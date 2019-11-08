#ifndef REESES_ONCHIP_H
#define REESES_ONCHIP_H

#include "reeses_config.h"
#include "reeses_training_unit.h"
#include "reeses_offchip.h"
#include "reeses_practical.h"

using namespace std;

namespace reeses {

// forward declaration to allow OnChipInfo
// to keep a pointer to the prefetcher
class ReesesPrefetcher;

class OnChipInfo {
    public:
        OnChipInfo();
        OnChipInfo(ReesesPrefetcher *pref);
        uint32_t train(address addr_A, TUEntry *data_B);
        bool get_structural_address(address addr, uint32_t &str_addr);
        bool get_physical_data(TUEntry* &data, uint32_t str_addr, bool clear_dirty=false);
        void access_off_chip(TUEntry *data, uint32_t str_addr, ocrt_t req_type);
        void update(TUEntry *data, uint32_t str_addr, bool set_dirty);
        void read_off_chip_region(uint32_t str_addr);
        void prefetch_metadata(uint32_t str_addr);
        vector<address> predict(pc cur_pc, address phy_addr, address last_addr, uint32_t dist);
        
        uint64_t ps_stats[AMC_SETS];
        uint64_t sp_stats[AMC_SETS];
    private:
        uint32_t get_ps_set(address phy_addr);
        uint32_t get_sp_set(uint32_t str_addr);

        void write_off_chip_region(uint32_t str_addr);
        void invalidate(address phy_addr, uint32_t str_addr);
        void evict(TUEntry *data, uint32_t str_addr, bool dirty);
        void increase_confidence(uint32_t str_addr);
        bool decrease_confidence(uint32_t str_addr);
        uint32_t assign_structural_addr();

        ReesesPrefetcher *prefetcher;
        OffChipInfo off_chip_info;
        map<address, PSEntry> ps_amc[AMC_SETS];
        map<uint32_t, SPEntry> sp_amc[AMC_SETS];
        uint64_t cur_timestamp;
};

}

#endif 
