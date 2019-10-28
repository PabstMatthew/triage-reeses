#ifndef __ISB_OFF_CHIP_CXX_HH__
#define __ISB_OFF_CHIP_CXX_HH__

#include <map>
#include <cassert>

typedef uint64_t Addr;
typedef uint32_t StrAddr;

typedef enum isb_repl_type_e {
    ISB_REPL_TYPE_LRU,
    ISB_REPL_TYPE_LFU,
    ISB_REPL_TYPE_BULKLRU,
    ISB_REPL_TYPE_BULKMETAPREF,
    ISB_REPL_TYPE_TLBSYNC,
    ISB_REPL_TYPE_METAPREF,
    ISB_REPL_TYPE_TLBSYNC_METAPREF,
    ISB_REPL_TYPE_TLBSYNC_BULKMETAPREF,
    ISB_REPL_TYPE_PERFECT,
    ISB_REPL_TYPE_MAX
} isb_repl_type_t;

typedef struct {
    int lookahead;
    int degree;
    unsigned amc_size;
    unsigned amc_assoc;
    isb_repl_type_t repl_policy;
    unsigned amc_repl_region_size;
    unsigned amc_repl_log_region_size;
    int amc_metapref_degree;
    unsigned log_cacheblocksize;
    bool isb_miss_prefetch_hit_only;
    bool isb_off_chip_ideal;
    bool isb_off_chip_writeback;
    bool count_off_chip_write_traffic;
    unsigned check_bandwidth;
    unsigned isb_off_chip_latency;
    unsigned isb_off_chip_fillers;
    unsigned prefetch_buffer_size;

    int bloom_region_shift_bits;
    int bloom_capacity;
    float bloom_fprate;
} pf_isb_conf_t;

typedef enum off_chip_req_type_e {
    ISB_OCI_REQ_LOAD_PS,
    ISB_OCI_REQ_LOAD_SP1,
    ISB_OCI_REQ_LOAD_SP2,
    ISB_OCI_REQ_STORE,
    ISB_OCI_REQ_MAX
} off_chip_req_type_t;

class OffChip_PS_Entry
{
  public:
    uint32_t str_addr;
    bool valid;
    unsigned confidence;
    bool cached;

    OffChip_PS_Entry() {
        reset();
    }

    void reset(){
        valid = false;
        str_addr = 0;
        confidence = 0;
        cached = true;
    }
    void set(uint32_t addr){
        reset();
        str_addr = addr;
        valid = true;
        confidence = 3;
        cached = true;
    }
    void increase_confidence(){
        assert(cached);
        confidence = (confidence == 3) ? confidence : (confidence+1);
    }
    bool lower_confidence(){
        assert(cached);
        confidence = (confidence == 0) ? confidence : (confidence-1);
        return confidence;
    }
    void mark_cached()
    {
       cached = true;
    }
    void mark_evicted()
    {
       cached = false;
    }

};

class OffChip_SP_Entry
{
  public:
    uint64_t phy_addr;
    bool valid;
    bool cached;

    void reset(){
        valid = false;
        phy_addr = 0;
        cached = true;
    }

    void set(uint64_t addr){
        phy_addr = addr;
        valid = true;
        cached = true;
    }
    void mark_cached()
    {
       cached = true;
    }
    void mark_evicted()
    {
       cached = false;
    }

};

class OffChipInfo
{
   public:
    std::map<uint64_t,OffChip_PS_Entry*> ps_map;
    std::map<uint32_t,OffChip_SP_Entry*> sp_map;

    uint64_t phy_access_count;
    uint64_t str_access_count;
    uint64_t phy_success_count;
    uint64_t str_success_count;
    uint64_t update_count;

    OffChipInfo();

    void reset();
    bool get_structural_address(uint64_t phy_addr, uint32_t& str_addr);
    bool get_physical_address(uint64_t& phy_addr, uint32_t str_addr);
    void update(uint64_t phy_addr, uint32_t str_addr);

    void invalidate(uint64_t phy_addr, uint32_t str_addr);
    void increase_confidence(uint64_t phy_addr);
    bool lower_confidence(uint64_t phy_addr);
    void mark_cached(uint64_t);
    void mark_evicted(uint64_t);
    bool exists_off_chip(uint64_t);
    void print();
    size_t get_ps_size();
    size_t get_sp_size();
};

#endif // __ISB_OFF_CHIP_CXX_HH__
