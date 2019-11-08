#ifndef REESES_PRACTICAL_H
#define REESES_PRACTICAL_H

#include "reeses_types.h"
#include "reeses_config.h"
#include "reeses_offchip.h"
#include "reeses_onchip.h"
#include "reeses_training_unit.h"
#include "reeses_offset_cache.h"
#include "reeses_stream.h"
#include "cache.h"

using namespace std;

namespace reeses {

struct MetadataRequest {
    pc miss_pc;
    bool to_ps;

    uint32_t str_addr;
    // for PS requests
    address phy_addr;
    set<address> phy_addrs;

    // for SP requests
    TUEntry *data;

    void ps_set(pc cur_pc, uint64_t phy, uint32_t sa) {
        miss_pc = cur_pc;
        to_ps = true;
        phy_addr = phy;
        phy_addrs.insert(phy);
        str_addr = sa;
    }

    void sp_set(uint32_t str, TUEntry *oc_data) {
        to_ps = false;
        str_addr = str;
        data = oc_data;
    }
};

class OnChipInfo;
class PrefetchStream;

struct ReesesPrefetcher {
    // structures
    CACHE *cache;
    TrainingUnit tu;
    OffsetCache offset_cache;
    OnChipInfo *on_chip_info;

    // internals
    address last_address;
    pc active_pc;
    map<pc, PrefetchStream> prefetch_buffer;
    set<address> metadata_read_requests;
    set<address> metadata_write_requests;
    map<string, stat> stats;
    set<uint32_t> str_addrs;
    map<pc, uint64_t> temporal_counts;
    map<pc, uint64_t> miss_counts;

    map<address, MetadataRequest> metadata_mapping;
    map<pc, deque<address>> stream_manager;

    /* entry-point functions */
    void initialize(CACHE *target_cache, bool footprint);
    void operate(address addr, pc cur_pc, bool cache_hit, uint8_t type);
    void cache_fill(address addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr);
    void final_stats();

    /* helper functions */
    void predict(pc cur_pc, address addr, address last_addr);
    void train(pc cur_pc, address addr);
    void complete_metadata_req(address metadata_addr);
    void read_metadata(address addr, TUEntry *data, uint32_t str_addr, bool to_ps);
    void write_metadata(address addr);
    void update_stream(pc cur_pc, address addr);

    /* unit tests */
    void test_delta_pattern();
    void test_footprints();
    void test_training_unit();
    void test_metadata_cache();
};

}

#endif
