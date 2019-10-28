#ifndef __MEM_CACHE_PREFETCH_ISB_HH__
#define __MEM_CACHE_PREFETCH_ISB_HH__

#include <cassert>
#include <map>
#include <vector>
#include <stdio.h>
#include <iostream>
#include <set>

#include "isb_onchip.h"
#include "isb_offchip.h"
#include "isb_training_unit.h"

#define MAX_STRUCTURAL_ADDR UINT_MAX - 1
#define STREAM_MAX_LENGTH 256
#define STREAM_MAX_LENGTH_BITS 8
#define BUFFER_SIZE 128

#define INVALID_ADDR 0xdeadbeef

//#define BLOOM_ISB
//#define BLOOM_ISB_TRAFFIC_DEBUG
#ifdef BLOOM_ISB
#include <bf/all.hpp>
#endif

extern const char *g_isb_repl_type_names[];
//#define OFF_CHIP_ONLY

struct IsbPrefetchBuffer
{
    uint32_t buffer[BUFFER_SIZE];
    bool valid[BUFFER_SIZE];
    uint32_t next_index;

    void reset(){
        for(uint32_t i=0; i<BUFFER_SIZE; i++)
            valid[i] = false;
        next_index = 0;
    }
    void add(uint32_t addr){
        buffer[next_index] = addr;
        valid[next_index] = true;
        next_index = (next_index + 1)%BUFFER_SIZE;
    }

    void issue(uint32_t i){
        assert(valid[i]);
        valid[i] = false;
    }
};


struct METADATAREQ
{
    std::set<uint64_t> phy_addrs;
    uint64_t phy_addr;
    uint32_t str_addr;
    bool ps;

    void set(uint64_t _phy, uint32_t _str, bool _ps)
    {
        ps = _ps;
        if(ps)
        {
            phy_addr = _phy;
            phy_addrs.insert(_phy);
        }
        else
            str_addr = _str;
        //phy_addr = _phy;
    }
};


class IsbPrefetcher
{
    private:
        TrainingUnit training_unit;
        IsbPrefetchBuffer prefetch_buffer;
        uint64_t alloc_counter;


        OffChipInfo off_chip_corr_matrix;
#ifndef OFF_CHIP_ONLY
        OnChipInfo on_chip_corr_matrix;
#endif
        uint64_t last_page;

        uint64_t not_found;
        uint64_t stream_divergence_count;
        uint64_t stream_divergence_count_offchip;

        //Stats
        uint64_t inval_count;
        uint64_t no_conflict_count;
        uint64_t same_stream_count;
        uint64_t new_stream_count;

        uint64_t stream_head;
        uint64_t new_addr;
        uint64_t same_addr;
        uint64_t triggers;
        uint64_t repeat;
        uint64_t predictions;
        uint64_t predict_init;
        uint64_t predict_stream_end;
        uint64_t buffer_success;
        uint64_t on_chip_mispredictions;

//        uint64_t off_chip_phy_request;
//        uint64_t off_chip_str_request;
        uint64_t on_chip_phy_found;
        uint64_t off_chip_phy_found;
        uint64_t on_chip_str_found;
        uint64_t off_chip_str_found;

        uint64_t pf_buffer_add;
        uint64_t pf_buffer_issue;

        uint64_t tlbsync_fetch_total;
        uint64_t tlbsync_fetch_actual;

        uint64_t new_stream_divergence, new_stream_endstream, new_stream_new_pc_addr;

        uint64_t stream_count, stream_length_count, stream_trigger_count, stream_trigger_region_count,
                 stream_region_count, stream_agg_region_count;
        std::map<uint64_t, uint64_t> trigger_addr_count;

        void isb_train_addr(uint64_t pc, bool str_addr_exists, uint64_t addr_B, uint32_t str_addr_B);
        uint32_t isb_train(uint32_t str_addr_A, uint64_t encoded_phy_addr_B, bool str_addr_B_exists, uint32_t str_addr_B);
        void count_stream_stats();

        int get_stream_length(uint32_t str_addr, uint64_t phy_addr);


        bool InitPrefetch(uint64_t candidate, uint16_t delay = 0);
        bool InitMetadataWrite(uint64_t candidate, uint16_t delay = 0);
        bool InitMetadataRead(uint64_t candidate, uint16_t delay = 0);

    public:

        #ifdef BLOOM_ISB
        bf::bloom_filter* off_chip_bloom_filter;// = new bf::basic_bloom_filter(0.99, 4000000);
        int bloom_region_shift_bits;
        int bloom_capacity;
        float bloom_fprate;
        //bf::basic_bloom_filter off_chip_bloom_filter(const double 0.8, 351000);

        uint64_t ps_offchip_bloom_incorrect;
        uint64_t ps_offchip_bloom_correct;
        uint64_t ps_not_offchip_bloom_correct;
        uint64_t ps_not_offchip_bloom_incorrect;
        uint64_t ps_bloom_wb_found;
        uint64_t ps_bloom_wb_not_found;
        uint64_t ps_bloom_found;
        uint64_t ps_bloom_not_found;
        #endif
        uint64_t ps_md_requests, sp_md_requests, write_md_requests;

        std::vector<uint64_t> prefetch_list;
        std::set<uint64_t> ideal_bloom_filter;
        std::set<uint64_t> metadata_read_requests;
        std::set<uint64_t> metadata_write_requests;
        std::map<uint64_t, METADATAREQ> metadata_mapping;
        #ifdef BLOOM_ISB
        bool lookup_bloom_filter(uint64_t phy_addr) {
          /* phy_addr is cache line addr - last 6 bits are zero - so simply right shift by 6 to prepare
           * Then right shift by region shift bits
           */
          return off_chip_bloom_filter->lookup(phy_addr >> 6 >> bloom_region_shift_bits);
        }
        void add_to_bloom_filter(uint64_t phy_addr) {
          off_chip_bloom_filter->add(phy_addr >> 6 >> bloom_region_shift_bits);
        }
        void set_bloom_region_shift_bits(int bl_rs) {
          bloom_region_shift_bits  =  bl_rs;
        }
        void set_bloom_capacity(int bl_ent) {
          bloom_capacity =  bl_ent;
        }
        int get_bloom_capacity() {
          return bloom_capacity;
        }
        void set_bloom_fprate(float bloom_acc) {
          bloom_fprate = bloom_acc;
        }
        void allocate_bloom_filter() {
          std::cout << "BFP: " << bloom_fprate << ", BCP: " << bloom_capacity << std::endl;
          off_chip_bloom_filter = new bf::basic_bloom_filter(bloom_fprate, bloom_capacity);
        }
        #endif


        unsigned lookahead, degree;
        unsigned log_cacheblocksize;
        unsigned prefetch_buffer_size;
        bool off_chip_writeback;
        bool count_off_chip_write_traffic;

        IsbPrefetcher(const pf_isb_conf_t *p);
        void set_conf(const pf_isb_conf_t *p);
        uint32_t assign_structural_addr();
        static double percentage(uint64_t a, uint64_t b)
        {
            return (100 *(double)a/(double)b );
        }

        bool issue_prefetch_buffer();

        void reset_stats();
//        void print_detailed_stats(tprinter_t *tp);
        void dump_stats();

        std::vector<uint64_t> informTLBEviction(uint64_t inserted_vaddr, uint64_t evicted_vaddr);

        void isb_predict(uint64_t, uint32_t);
        void calculatePrefetch(uint64_t addr, uint64_t pc, bool hit, uint64_t* prefetch_addresses, int prefetch_addresses_size);
//        void handle_metadata_state(control_t* data);
        void read_metadata(uint64_t, uint64_t, uint32_t, off_chip_req_type_t);
        void write_metadata(uint64_t, off_chip_req_type_t);
        void complete_metadata_req(uint64_t);
};

#endif // __MEM_CACHE_PREFETCH_ISB_HH

