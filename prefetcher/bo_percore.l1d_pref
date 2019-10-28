#include "bo_percore.h" 
#include <stdint.h>

#define DEGREE 1

void CACHE::l1d_prefetcher_initialize() {
    cout << "CPU " << cpu << " L2C BO prefetcher" << endl;
	bo_l2c_prefetcher_initialize();
}

void CACHE::l1d_prefetcher_operate(uint64_t addr, uint64_t pc, uint8_t cache_hit, uint8_t type)
{

    uint64_t bo_trigger_addr = 0;
    uint64_t bo_target_offset = 0;
    uint64_t bo_target_addr = 0;
    bo_l2c_prefetcher_operate(addr, pc, cache_hit, type, this, &bo_trigger_addr, &bo_target_offset, 0);

    if (bo_trigger_addr && bo_target_offset) {

        for(unsigned int i=1; i<=DEGREE; i++) {
            bo_target_addr = bo_trigger_addr + (i*bo_target_offset); 
            bo_issue_prefetcher(this, pc, bo_trigger_addr, bo_target_addr, FILL_L1);
        }
    }
}

void CACHE::l1d_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint64_t metadata_in)
{
	bo_l2c_prefetcher_cache_fill(addr, set, way, prefetch, evicted_addr, this, 0);
}

void CACHE::l1d_prefetcher_final_stats() {
	bo_l2c_prefetcher_final_stats();
}

