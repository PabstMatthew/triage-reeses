
#include "cache.h"
#include "triage.h"
#include <map>

#define TRIAGE_FILL_LEVEL FILL_L2
#define MAX_ALLOWED_DEGREE 8

TriageConfig conf[NUM_CPUS];
Triage data[NUM_CPUS];
uint64_t last_address[NUM_CPUS];

std::set<uint64_t>unique_addr;
std::map<uint64_t, uint64_t> total_usage_count;
std::map<uint64_t, uint64_t> actual_usage_count;

//16K entries = 64KB
void triage_prefetcher_initialize(CACHE *cache) {
    uint32_t cpu = cache->cpu;
    conf[cpu].lookahead = 1;
    conf[cpu].degree = 1;
    conf[cpu].on_chip_assoc = 8;
    conf[cpu].training_unit_size = 10000000;
    //conf[cpu].repl = TRIAGE_REPL_LRU;
    conf[cpu].repl = TRIAGE_REPL_HAWKEYE;
    //conf[cpu].repl = TRIAGE_REPL_PERFECT;
    conf[cpu].use_dynamic_assoc = true;
    conf[cpu].on_chip_assoc = L2C_WAY;
    conf[cpu].on_chip_set = 32768;
    std::cout << "CPU " << cpu << " assoc: " << conf[cpu].on_chip_assoc << std::endl;

    data[cpu].set_conf(&conf[cpu]);
}

uint64_t triage_prefetcher_operate(uint64_t addr, uint64_t pc, uint8_t cache_hit, uint8_t type, uint64_t metadata_in, CACHE *cache) {
    if (type != LOAD)
        return metadata_in;

    //if (cache_hit)
        //return metadata_in;

    uint32_t cpu = cache->cpu;
    addr >>= LOG2_BLOCK_SIZE;
    //addr <<= LOG2_BLOCK_SIZE;
    if (addr == last_address[cpu])
        return metadata_in;
    last_address[cpu] = addr;
    unique_addr.insert(addr);

    // clear the prefetch list
    uint64_t prefetch_addr_list[MAX_ALLOWED_DEGREE];
    for (int i = 0; i < MAX_ALLOWED_DEGREE; i++)
        prefetch_addr_list[i] = 0;

    // set the prefetch list by operating the prefetcher
    data[cpu].calculatePrefetch(pc, addr, cache_hit, prefetch_addr_list, MAX_ALLOWED_DEGREE, cpu);

    // prefetch desired lines
    int prefetched = 0;
    for (int i = 0; i < MAX_ALLOWED_DEGREE; i++) {
        uint64_t target = prefetch_addr_list[i] << LOG2_BLOCK_SIZE; 

        // check if prefetch requested
        if (target == 0)
            break;

        // check L2 and LLC for request to keep track of which metadata is used
        PACKET test_packet;
        test_packet.address = prefetch_addr_list[i];
        test_packet.full_addr = target;
        bool llc_hit = static_cast<CACHE*>(cache->lower_level)->check_hit(&test_packet) != -1;
        bool l2_hit = cache->check_hit(&test_packet) != -1;
        uint64_t md_in = addr;
        if (llc_hit)
            md_in = 0;
        total_usage_count[addr]++;
        if (!l2_hit && !llc_hit)
           actual_usage_count[addr]++; 
            
        // check if prefetch actually issued
        if (cache->prefetch_line(pc, addr, target, TRIAGE_FILL_LEVEL, md_in)) {
            prefetched++;
            if (prefetched >= conf[cpu].degree)
                break;
        }
    }

    // Set cache assoc if dynamic
    uint32_t total_assoc = 0;
    for (uint32_t mycpu = 0; mycpu < NUM_CPUS; mycpu++)
        total_assoc += data[mycpu].get_assoc();
    total_assoc /= NUM_CPUS;

    // set associativity
    assert(total_assoc < LLC_WAY);
    if (conf[cpu].repl != TRIAGE_REPL_PERFECT)
        static_cast<CACHE*>(cache->lower_level)->current_assoc = LLC_WAY - total_assoc;

    return metadata_in;
}

uint64_t triage_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint64_t metadata_in, CACHE *cache) {
    uint32_t cpu = cache->cpu;
    if (prefetch) {
        uint64_t next_addr;
        bool next_addr_exists = data[cpu].on_chip_data.get_next_addr(metadata_in, next_addr, 0, true);
        //cout << "Filled " << hex << addr << "  by " << metadata_in << " " << next_addr_exists << endl;
    }
    return metadata_in;
}

void triage_prefetcher_final_stats(CACHE *cache) {
    uint32_t cpu = cache->cpu;
    cout << "CPU " << cpu << " TRIAGE Stats:" << endl;

    data[cpu].print_stats();

    std::map<uint64_t, uint64_t> total_pref_count;
    std::map<uint64_t, uint64_t> actual_pref_count;
    for (auto it = total_usage_count.begin(); it != total_usage_count.end(); it++)
        total_pref_count[it->second]++;
    for (auto it = actual_usage_count.begin(); it != actual_usage_count.end(); it++)
        actual_pref_count[it->second]++;

    cout << "Unique Addr Size: " << unique_addr.size() << endl;
}

