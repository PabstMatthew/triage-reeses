#include "reeses_onchip.h"

using namespace std;

namespace reeses {

OnChipInfo::OnChipInfo() : cur_timestamp(0) {
    for (uint32_t i = 0; i < AMC_SETS; i++) {
        ps_stats[i] = 0;
        sp_stats[i] = 0;
    }
}

OnChipInfo::OnChipInfo(ReesesPrefetcher *pref) : prefetcher(pref) {
    OnChipInfo();
}

uint32_t OnChipInfo::get_ps_set(address phy_addr) {
    return phy_addr % AMC_SETS;
}

uint32_t OnChipInfo::get_sp_set(uint32_t str_addr) {
    uint32_t pos_hash = str_addr;
    uint32_t stream_hash = (str_addr >> LOG2_MAX_STREAM_LENGTH);
    return (pos_hash ^ stream_hash) % AMC_SETS;
}

bool OnChipInfo::get_structural_address(address phy_addr, uint32_t &str_addr) {
    cur_timestamp++;

    uint32_t set = get_ps_set(phy_addr);
    map<address, PSEntry> &ps_set = ps_amc[set];
    if (ps_set.find(phy_addr) == ps_set.end()) {
        // no entry in PS
        return false;
    } else if (ps_set[phy_addr].valid) {
        // found valid entry in PS
        str_addr = ps_set[phy_addr].str_addr;
        ps_set[phy_addr].last_access = cur_timestamp;
        return true;
    } else {
        // invalidated entry in PS
        return false;
    }
}

bool OnChipInfo::get_physical_data(TUEntry* &data, uint32_t str_addr, bool clear_dirty) {
    cur_timestamp++;

    uint32_t set = get_sp_set(str_addr);
    map<uint32_t, SPEntry> &sp_set = sp_amc[set]; 
    if (sp_set.find(str_addr) == sp_set.end()) {
        // no entry in SP
        return false;
    } else if (sp_set[str_addr].valid) {
        // found valid entry in SP
        data = sp_set[str_addr].data;
        if (data == nullptr) {
            cerr << "invalid str addr " << str_addr << endl;
            assert(0);
        }
        sp_set[str_addr].last_access = cur_timestamp;
        if (clear_dirty)
            sp_set[str_addr].dirty = false;
        return true;
    } else { 
        // invalidated entry in SP
        return false;
    }
}

void OnChipInfo::prefetch_metadata(uint32_t str_addr) {
    for (uint32_t i = 1; i <= METADATA_DEGREE; i++) {
        uint32_t pref_str_addr = str_addr + i;
        TUEntry *exist_data = nullptr;
        bool phy_on_chip_exist = get_physical_data(exist_data, pref_str_addr);
        if (!phy_on_chip_exist) {
            access_off_chip(exist_data, pref_str_addr, OCI_REQ_LOAD_SP);
            // TODO read off chip region
        }
    }
}

void OnChipInfo::access_off_chip(TUEntry *data, uint32_t str_addr, ocrt_t req_type) {
    if (req_type == OCI_REQ_STORE) {
        address metadata_addr = str_addr >> 3;
        // TODO: add to bloom filter
        prefetcher->write_metadata(metadata_addr);
    } else {
        if (req_type == OCI_REQ_LOAD_SP) {
            address metadata_addr = str_addr >> 3;
            if (!off_chip_info.get_physical_data(data, str_addr)) {
                data = nullptr;
            } else {
                data = data->clone();
            }

            if (data == nullptr && IDEAL_TRAFFIC)
                return;
            // TODO: lookup bloom filter
            prefetcher->read_metadata(metadata_addr, data, str_addr, false);
        } else if (req_type == OCI_REQ_LOAD_PS) {
            assert(data != nullptr);
            address phy_addr = (data->has_spatial) ? data->spatial->last_address() : data->temporal;
            address metadata_addr = phy_addr >> 3;
            if (!off_chip_info.get_structural_address(phy_addr, str_addr))
                str_addr = INVALID_STR_ADDR;

            if (str_addr == INVALID_STR_ADDR && IDEAL_TRAFFIC)
                return;
            // TODO: lookup bloom filter
            prefetcher->read_metadata(metadata_addr, data, str_addr, true);
        }
    }
}

uint32_t OnChipInfo::train(address addr_A, TUEntry *data_B) {
    assert(data_B != nullptr);

    // find SA of A
    uint32_t str_addr_A = 0;
    if (!get_structural_address(addr_A, str_addr_A)) {
        D(cout << "\t\t\tcreating  address for trigger" << endl;)
        prefetcher->stats["new_streams"] += 1;
        str_addr_A = assign_structural_addr();
        update(new TUEntry(addr_A), str_addr_A, true);
    }

    uint32_t str_addr_B = 0;
    address addr_B = (data_B->has_spatial) ? data_B->spatial->last_address() : data_B->temporal;
    bool str_addr_B_exists = get_structural_address(addr_B, str_addr_B);

    // If SA(A) is at a stream boundary return, B is as good as a stream start
    if ((str_addr_A+1) % MAX_STREAM_LENGTH == 0) {
        D(cout << "\t\t\tstr addr A is stream boundary" << endl;)
        if (!str_addr_B_exists) {
            str_addr_B = assign_structural_addr();
            update(data_B, str_addr_B, true);
        } else {
            delete data_B;
        }
        return str_addr_B;
    }

    bool invalidated = false;
    if (str_addr_B_exists) {
        D(cout << "\t\t\tfound structural address " << str_addr_B << " for B" << endl;)
        if ((str_addr_B % MAX_STREAM_LENGTH) == ((str_addr_A+1) % MAX_STREAM_LENGTH)) {
            D(cout << "\t\t\tB follows A in the SA space" << endl;)
            increase_confidence(str_addr_B);
            delete data_B;
            return str_addr_B;
        } else {
            // TODO move spatial equality check here
            D(cout << "\t\t\tB does not follow A in the SA space" << endl;)
            if (decrease_confidence(str_addr_B)) {
                delete data_B;
                return str_addr_B;
            }
            // lost confidence in B's mapping
            address addr_B = data_B->has_spatial ? data_B->spatial->last_address() : data_B->temporal;
            invalidate(addr_B, str_addr_B);
            invalidated = true;
            str_addr_B_exists = false;
        }
    }

    assert(!str_addr_B_exists);
    D(cout << "\t\t\tno str addr for B" << endl;)

    // Handle stream divergence (and spatial patterns)
    TUEntry *data_Aplus1 = nullptr;
    bool data_Aplus1_exists = get_physical_data(data_Aplus1, str_addr_A+1);
    bool data_Aplus1_exists_off_chip = off_chip_info.get_physical_data(data_Aplus1, str_addr_A+1);

    if (data_Aplus1_exists || data_Aplus1_exists_off_chip) {
        if (invalidated || (*data_Aplus1) == (*data_B)) {
            D(cout << "\t\t\tB invalidated or already follows A (spatial)" << endl;)
            delete data_B;
            return str_addr_B;
        } else {
            str_addr_B = assign_structural_addr();
            D(cout << "\t\t\tassigning new str addr " << str_addr_B << "to B" << endl;)
            update(data_B, str_addr_B, true);
            return str_addr_B;
        }
    } else {
        D(cout << "\t\t\tassigning B SA(A)+1" << endl;)
        str_addr_B = str_addr_A+1;
        update(data_B, str_addr_B, true);
        return str_addr_B;
    }
}

void OnChipInfo::update(TUEntry *data, uint32_t str_addr, bool set_dirty) {
    assert(data != nullptr);
    prefetcher->str_addrs.insert(str_addr);

    // update PS
    address addr = (data->has_spatial) ? data->spatial->last_address() : data->temporal;
    D(cout << "\t\t\tgiving address " << hex << addr << dec << " str addr " << str_addr << endl;)
    D(cout << "\t\t\tPS set: " << get_ps_set(addr) << " SP set: " << get_sp_set(str_addr) << endl;)
    uint32_t set = get_ps_set(addr);
    ps_stats[set] += 1;
    map<address, PSEntry> &ps_set = ps_amc[set]; 
    if (ps_set.find(addr) == ps_set.end()) {
        if (!IDEAL_AMC && ps_set.size() == AMC_WAYS) {
            // eviction needed
            uint64_t lru_pos = -1;
            address lru_key = 0;
            for (auto const &entry : ps_set) {
                if (!entry.second.valid) {
                    lru_pos = 0;
                    lru_key = entry.first;
                } else if (entry.second.last_access < lru_pos) {
                    lru_pos = entry.second.last_access;
                    lru_key = entry.first;
                }
            }
            assert(lru_key != 0);
            uint32_t evicted_str_addr = ps_set[lru_key].str_addr;
            sp_amc[get_sp_set(evicted_str_addr)][evicted_str_addr].valid = false;
            ps_set.erase(lru_key);
        }
        ps_set[addr] = PSEntry();
        ps_set[addr].set(str_addr);
        ps_set[addr].last_access = cur_timestamp;
    } else if (ps_set[addr].str_addr != str_addr) {
        ps_set[addr].set(str_addr);
        ps_set[addr].last_access = cur_timestamp;
    } else {
        ps_set[addr].last_access = cur_timestamp;
    }

    // update SP
    set = get_sp_set(str_addr);
    sp_stats[set] += 1;
    map<uint32_t, SPEntry> &sp_set = sp_amc[set];
    if (sp_set.find(str_addr) == sp_set.end()) {
        if (!IDEAL_AMC && sp_set.size() == AMC_WAYS) {
            // eviction needed
            uint64_t lru_pos = -1;
            uint32_t lru_key = INVALID_STR_ADDR;
            for (auto const &entry : sp_set) {
                if (!entry.second.valid) {
                    lru_pos = 0;
                    lru_key = entry.first;
                } else if (entry.second.last_access < lru_pos) {
                    lru_pos = entry.second.last_access;
                    lru_key = entry.first;
                }
            }
            assert(lru_key != INVALID_STR_ADDR);
            bool dirty = sp_set[lru_key].dirty;
            TUEntry *data = sp_set[lru_key].data;
            D(cout << "\t\t\tSP evicting str addr " << lru_key << " dirty: " << dirty << endl;)
            evict(data, lru_key, dirty);
        }
        sp_set[str_addr] = SPEntry();
        sp_set[str_addr].set(data);
        sp_set[str_addr].dirty = set_dirty;
        sp_set[str_addr].last_access = cur_timestamp;
    } else {
        if (!((*sp_set[str_addr].data) == (*data)))
            sp_set[str_addr].dirty = set_dirty;
        sp_set[str_addr].set(data);
        sp_set[str_addr].last_access = cur_timestamp;
    }
}

void OnChipInfo::invalidate(address phy_addr, uint32_t str_addr) {
    D(cout << "\t\t\tinvalidating phy addr " << hex << phy_addr << dec << " with str addr " << str_addr << endl;)

    // invalidate PS entry, if necessary
    uint32_t ps_set = get_ps_set(phy_addr);
    ps_amc[ps_set].erase(phy_addr);

    // invalidate SP entry
    uint32_t sp_set = get_sp_set(str_addr);
    sp_amc[sp_set][str_addr].reset();
    sp_amc[sp_set].erase(str_addr);
}

void OnChipInfo::evict(TUEntry *data, uint32_t str_addr, bool dirty) {
    assert(data != nullptr);

    if (dirty) {
        access_off_chip(data, str_addr, OCI_REQ_STORE);
        write_off_chip_region(str_addr);
    }
    address phy_addr;
    if (data->has_spatial)
        phy_addr = data->spatial->last_address();
    else
        phy_addr = data->temporal;
    invalidate(phy_addr, str_addr);
}
        
void OnChipInfo::write_off_chip_region(uint32_t str_addr) {
    uint32_t base_str_addr = (str_addr >> 3) << 3;
    for (uint32_t i = 0; i < 8; i++) {
        TUEntry *data;
        uint32_t target_str_addr = base_str_addr + i;
        if (get_physical_data(data, target_str_addr, true)) {
            off_chip_info.update(data->clone(), target_str_addr);
        }
    }
}
        
void OnChipInfo::read_off_chip_region(uint32_t str_addr) {
    uint32_t base_str_addr = (str_addr >> 3) << 3;
    for (uint32_t i = 0; i < 8; i++) {
        TUEntry *data;
        uint32_t target_str_addr = base_str_addr + i;
        if (off_chip_info.get_physical_data(data, target_str_addr)) {
            update(data->clone(), target_str_addr, false);
        }
    }
}

void OnChipInfo::increase_confidence(uint32_t str_addr) {
    uint32_t set = get_sp_set(str_addr);
    map<uint32_t, SPEntry> &sp_set = sp_amc[set]; 

    assert(sp_set.find(str_addr) != sp_set.end());
    assert(sp_set[str_addr].valid);

    sp_set[str_addr].inc();
}

bool OnChipInfo::decrease_confidence(uint32_t str_addr) {
    uint32_t set = get_sp_set(str_addr);
    map<uint32_t, SPEntry> &sp_set = sp_amc[set]; 

    assert(sp_set.find(str_addr) != sp_set.end());
    assert(sp_set[str_addr].valid);

    return sp_set[str_addr].dec();
}

uint32_t OnChipInfo::assign_structural_addr() {
    static uint32_t alloc_counter = 0;
    alloc_counter += MAX_STREAM_LENGTH;
    return alloc_counter - MAX_STREAM_LENGTH;
}

vector<address> OnChipInfo::predict(pc cur_pc, address phy_addr, address last_addr, uint32_t dist) {
    vector<address> result;

    uint32_t str_addr = INVALID_STR_ADDR;
    get_structural_address(phy_addr, str_addr);

    // generate new predictions
    for (uint32_t i = 1; i <= dist; i++) {
        uint32_t str_addr_candidate = str_addr+i;
        TUEntry *prediction = nullptr;
        bool metadata_on_chip = str_addr != INVALID_STR_ADDR && get_physical_data(prediction, str_addr_candidate);
        if (str_addr_candidate % MAX_STREAM_LENGTH == 0) {
            prefetcher->stats["predict_stream_end"] += 1;
            metadata_on_chip = false;
        }
        bool region_cross = (last_addr >> LOG2_REGION_SIZE) != (phy_addr >> LOG2_REGION_SIZE); 
        if (!metadata_on_chip) {
            // try offset prediction
            if (region_cross) {
                D(cout << "\t\ttrying offset prediction" << endl;)
                prefetcher->stats["offset_inits"] += 1;
                if (prefetcher->offset_cache.lookup(cur_pc, phy_addr, prediction)) {
                    metadata_on_chip = true;
                    prefetcher->stats["offset_hits"] += 1;
                }
            }

        }
        
        // try to prefetch metadata
        prefetch_metadata(str_addr_candidate);

        // generate prefetch address from metadata
        if (metadata_on_chip) {
            assert(prediction != nullptr);

            if (prediction->has_spatial) {
                // spatial pattern in metadata
                D(cout << "\t\tpredicted spatial pattern" << endl;)
                SpatialPattern *pattern = prediction->spatial;
                vector<address> preds = pattern->predict(phy_addr);
                for (address pred : preds) {
                    D(cout << "\t\tpredicted address: " << hex << pred << dec << endl;)
                    if (!NO_SPATIAL_PF) {
                        result.push_back(pred);
                        prefetcher->stats["predicted_spatials"] += 1;
                    }
                    last_addr = phy_addr;
                    phy_addr = pred;
                }
            } else {
                // temporal address in metadata
                D(cout << "\t\tpredicted temporal: " << hex << prediction->temporal << dec << endl;)
                if (!NO_TEMPORAL_PF) {
                    result.push_back(prediction->temporal);
                    prefetcher->stats["predicted_temporals"] += 1;
                }
                last_addr = phy_addr;
                phy_addr = prediction->temporal;
            }
        } else {
            return result;
        }
    }
    return result;
}

}
