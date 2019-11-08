#include "reeses_practical.h"
#include "../spp.h"
#include "uncore.h"
#include <algorithm>

//#define REESES_TESTS

#define BO_DEGREE 4

using namespace std;

extern UNCORE uncore;

namespace reeses {

void ReesesPrefetcher::initialize(CACHE *target_cache, bool footprint) {
    spp_prefetcher_initialize();
    cache = target_cache;
    tu = TrainingUnit(footprint);
    offset_cache = OffsetCache();
    on_chip_info = new OnChipInfo(this);

#ifdef REESES_TESTS
    test_delta_patterns();
    test_footprint();
    test_training_unit();
    test_metadata_cache();
    cout << "REESES: finished all tests successfully!" << endl;
#endif

    cout << "REESES: starting spatio-temporal prefetching with ";
    if (footprint) cout << "footprints";
    else cout << "delta patterns";
    cout << ", lookahead " << LOOKAHEAD << endl;
}

void ReesesPrefetcher::operate(address addr, pc cur_pc, bool cache_hit, uint8_t type) {
    uint32_t str_addr = INVALID_STR_ADDR;
    bool str_addr_exists = on_chip_info->get_structural_address(addr >> 6, str_addr);

    uint32_t threshold = 90;
    uint64_t filled_pf = uncore.LLC.pf_fill;
    uint64_t cycles = current_core_cycle[cache->cpu];
    double traffic = filled_pf*1.0/cycles;

    switch (tu.spatial_counters[cur_pc]) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
            threshold = 90;
            break;
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
            threshold = 80; 
            break;
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 15:
            threshold = 70; 
    }

    if (!NO_COMPULSORY_PF && !str_addr_exists)
        spp_prefetcher_operate(addr, cur_pc, cache_hit, type, cache, threshold);

    // only consider demand misses 
    if (type != LOAD || cache_hit)
        return;

    // convert to block addr
    addr >>= LOG2_BLOCK_SIZE;

    // ignore repeating addresses
    if (addr == last_address)
        return;
    last_address = addr;
    active_pc = cur_pc;

    // clear queues
    metadata_read_requests.clear();
    metadata_write_requests.clear();
    
    stats["triggers"] += 1;
    D(cout << "miss on addr " << hex << addr << " with pc " << cur_pc << dec << endl;)

    /* metadata management */
    if (str_addr_exists) {
        stats["on_chip_hit"] += 1;
        D(cout << "\tfound structural address " << str_addr << " on-chip" << endl;)
    }

    // fetch metadata from off-chip
    if (!str_addr_exists) {
        D(cout << "\taccessing off-chip" << endl;)
        TUEntry tu_entry = TUEntry(addr);
        on_chip_info->access_off_chip(&tu_entry, str_addr, OCI_REQ_LOAD_PS);
    }

    /* training */
    D(cout << "\ttraining" << endl;)
    train(cur_pc, addr);
    
    /* prediction */
    if (prefetch_buffer.find(cur_pc) == prefetch_buffer.end())
        prefetch_buffer[cur_pc] = PrefetchStream(cur_pc, cache, on_chip_info, &tu, this); 
    prefetch_buffer[cur_pc].update(addr);

    // issue metadata requests
    D(cout << "\tissuing metadata requests" << endl;)
    for (address metadata_addr : metadata_read_requests)
        cache->read_metadata(metadata_addr);
    
    for (address metadata_addr : metadata_write_requests)
        cache->write_metadata(metadata_addr);
}

void ReesesPrefetcher::cache_fill(address addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr) {
    spp_prefetcher_cache_fill(addr, set, way, prefetch, evicted_addr);
}

void ReesesPrefetcher::final_stats() { 
    cout << "REESES CPU " << cache->cpu << " STATS:" << endl;
    for (auto const &entry : stats)
        cout << entry.first << ": " << entry.second << endl;

    cout << "str addrs assigned: " << str_addrs.size() << endl;
    for (auto const &entry : miss_counts) {
        pc cur_pc = entry.first;
        uint64_t total = entry.second;
        uint64_t temporal = temporal_counts[cur_pc];
        uint64_t spatial = total - temporal;
        double percent_spatial = spatial*1.0/total;
        cout << "pc: " << cur_pc << " misses: " << total << " percent_spatial: " << percent_spatial << endl;
    }

    uint64_t ps_total = 0;
    uint64_t sp_total = 0;
    for (uint32_t i = 0; i < AMC_SETS; i++) {
        ps_total += on_chip_info->ps_stats[i];
        sp_total += on_chip_info->sp_stats[i];
    }

    for (uint32_t i = 0; i < AMC_SETS; i++) {
        double expected = ps_total*1.0/AMC_SETS;
        double actual = on_chip_info->ps_stats[i]*1.0;
        cout << "PS set " << i << ": " << (actual/expected) << endl;
    }
    cout << endl;
    for (uint32_t i = 0; i < AMC_SETS; i++) {
        double expected = sp_total*1.0/AMC_SETS;
        double actual = on_chip_info->sp_stats[i]*1.0;
        cout << "SP set " << i << ": " << (actual/expected) << endl;
    }
    cout << endl;
}

void ReesesPrefetcher::train(pc cur_pc, address addr) {
    miss_counts[cur_pc] += 1;

    // get new correlated pair from training unit
    TUEntry *result = tu.update(cur_pc, addr);
    if (result != nullptr) {
        address trigger = result->temporal;
        D(cout << "\t\tgot TUEntry with trigger " << hex << trigger << dec << ": ";)
        if (!result->has_spatial) {
            // correct ordering for temporal entries
            result->temporal = addr;
            stats["temporal"] += 1;
            D(cout << "temporal" << endl;)
            temporal_counts[cur_pc] += 1;
            /*
        } else if (result->spatial->size() == 1) {
            D(cout << "small spatial";)
            // convert small deltas to a temporal
            // this seems to hurt regular benchmarks by 2%
            stats["temporal"] += 2;
            address first = trigger;
            address second = result->spatial->last_address();

            D(cout << endl << "training small spatial on-chip" << endl;)
            TUEntry *second_entry = new TUEntry(second);
            on_chip_info->train(first, second_entry);

            // setup next call to train()
            trigger = second;
            delete result;
            result = new TUEntry(addr);
            */
        } else {
            D(cout << "spatial" << endl;)
            //offset_cache.insert(cur_pc, trigger, result->clone());
            stats["spatial"] += result->spatial->size();
        }

        // add new pair to out cache
        D(cout << "\t\ttraining on-chip" << endl;)
        on_chip_info->train(trigger, result->clone());
        
        // link end of spatials to next temporal
        if (!tu.FOOTPRINT && result->has_spatial) {
            D(cout << "\t\tlinking end of spatial" << endl;)
            address last_addr = result->spatial->last_address();
            TUEntry *link = new TUEntry(addr);
            on_chip_info->train(last_addr, link);

            // TODO check if this optimization helps
            int32_t delta = addr-last_addr;
            if (delta >= -REGION_SIZE && delta < REGION_SIZE) {
                SpatialPattern *spatial = new DeltaPattern(delta, last_addr);
                TUEntry *offset_link = new TUEntry(last_addr);
                offset_link->spatial = spatial;
                offset_link->has_spatial = true;
                D(cout << "\t\tadding link to offset cache" << endl;)
                //offset_cache.insert(cur_pc, last_addr, offset_link);
            }
        }
        delete result;
    }
}

void ReesesPrefetcher::complete_metadata_req(address metadata_addr) {
    D(cout << "\tcompleting metadata request" << endl;)

    // clear queues
    metadata_read_requests.clear();
    metadata_write_requests.clear();
    
    D(cout << "\t\tcompleting metadata request for " << metadata_addr << endl;)
    if (metadata_mapping.find(metadata_addr) == metadata_mapping.end()) {
        D(cout << "\t\trequest not found in mapping" << endl;)
        return;
    }

    uint32_t str_addr = metadata_mapping[metadata_addr].str_addr;
    if (metadata_mapping[metadata_addr].to_ps) {
        if (str_addr != INVALID_STR_ADDR) {
            D(cout << "\t\tupdating on-chip cache with new PS mapping" << endl;)
            on_chip_info->read_off_chip_region(str_addr);
        }
        // issue dependent prefetches
        for (address phy_addr : metadata_mapping[metadata_addr].phy_addrs) {
            stats["metadata_request_pred_inits"] += 1;
            pc cur_pc = metadata_mapping[metadata_addr].miss_pc;
            // TODO should we update the stream manager here?
            if (tu.data.count(cur_pc) != 0 && !tu.data[cur_pc]->has_spatial)
                prefetch_buffer[cur_pc].update(phy_addr);

            //address pred = phy_addr << LOG2_BLOCK_SIZE;
            //cache->prefetch_line(metadata_mapping[metadata_addr].miss_pc, pred, pred, FILL_LLC);
        }
    } else {
        TUEntry *data = metadata_mapping[metadata_addr].data;
        if (data != nullptr) {
            D(cout << "\t\tupdating on-chip cache with new SP mapping" << endl;)
            on_chip_info->read_off_chip_region(str_addr);
        }
    }
    metadata_mapping.erase(metadata_addr);
    
    // issue metadata requests
    for (address metadata_addr : metadata_read_requests)
        cache->read_metadata(metadata_addr);
    
    for (address metadata_addr : metadata_write_requests)
        cache->write_metadata(metadata_addr);
}

void ReesesPrefetcher::write_metadata(address metadata_addr) {
    static const uint64_t crcPolynomial = 3988292384ULL;
    metadata_addr ^= crcPolynomial;
    
    if (metadata_write_requests.find(metadata_addr) == metadata_write_requests.end()) {
        D(cout << "\t\tqueuing write request to " << metadata_addr << endl;)
    } else {
        return;
    }

    stats["write_requests"] += 1;
    metadata_write_requests.insert(metadata_addr);
}

void ReesesPrefetcher::read_metadata(address metadata_addr, TUEntry *data, uint32_t str_addr, bool to_ps) {
    static const uint64_t crcPolynomial = 3988292384ULL;
    metadata_addr ^= crcPolynomial;
    
    if (metadata_mapping.find(metadata_addr) != metadata_mapping.end())
        return;

    if (metadata_read_requests.find(metadata_addr) == metadata_read_requests.end()) {
        D(cout << "\t\tqueuing read request to " << metadata_addr << endl;)
    } else {
        return;
    }

    if (to_ps) {
        stats["ps_read_requests"] += 1;
        address phy_addr = (data->has_spatial) ? data->spatial->last_address() : data->temporal;
        metadata_mapping[metadata_addr].ps_set(active_pc, phy_addr, str_addr);
    } else {
        stats["sp_read_requests"] += 1;
        metadata_mapping[metadata_addr].sp_set(str_addr, data);
    }

    metadata_read_requests.insert(metadata_addr);
}

}

