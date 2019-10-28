
#include <iostream>
#include <set>
#include "isb.h"
#include "cache.h"
#include "isb_training_unit.h"

using namespace std;

//#define DEBUG
//#define SHOW_DETAIL

#ifdef DEBUG
#define debug_cout cerr << "[ISB] "
#else
#define debug_cout if (0) cerr
#endif

#define outf &cerr

//#define IDEAL
//#define PS_FIX
//#define PS_FIX_PREDICTION

bool IsbPrefetcher::InitPrefetch(uint64_t candidate, uint16_t delay)
{
    prefetch_list.push_back(candidate);
    return true;
}

void IsbPrefetcher::isb_train_addr(uint64_t pc, bool str_addr_exists, uint64_t addr_B, uint32_t str_addr_B)
{
    uint32_t str_addr_A;
    uint64_t addr_A;
    if (training_unit.access(pc, addr_A, str_addr_A, addr_B))
    {
        if (addr_A == addr_B)
        {
            repeat++;
            return;
        }
        str_addr_B = isb_train(str_addr_A, addr_B, str_addr_exists, str_addr_B);
        //cout << "New str_addr: " << hex << str_addr_B << endl;
    }
    else if (!str_addr_exists) {
        new_addr++;
        ++new_stream_new_pc_addr;
        str_addr_B = assign_structural_addr();
#ifdef OFF_CHIP_ONLY
        off_chip_corr_matrix.update(addr_B, str_addr_B);
#else
        on_chip_corr_matrix.update(addr_B, str_addr_B, true);
        if (on_chip_corr_matrix.repl_policy != ISB_REPL_TYPE_PERFECT && !off_chip_writeback) {
            off_chip_corr_matrix.update(addr_B, str_addr_B);
            #ifdef BLOOM_ISB
              #ifdef BLOOM_ISB_TRAFFIC_DEBUG
              //printf("Bloom add c: 0x%lx\n", addr_B);
              #endif
            //add_to_bloom_filter(addr_B);
            #endif
            if (count_off_chip_write_traffic) {
                on_chip_corr_matrix.access_off_chip(addr_B, str_addr_B, ISB_OCI_REQ_STORE);
            }
            ++off_chip_corr_matrix.update_count;
        }
#endif
#ifdef DEBUG
        debug_cout << hex << addr_B
            << " allotted a structural address of " << str_addr_B << endl;
#endif
        //cout << "New str_addr: " << hex << str_addr_B << endl;
    }

    // Update the training unit for the next access
    training_unit.update(pc, addr_B, str_addr_B);
}

uint32_t IsbPrefetcher::isb_train(uint32_t str_addr_A, uint64_t phy_addr_B, bool str_addr_B_exists, uint32_t str_addr_B)
{
    //Algorithm for training correlated pair (A,B)
    //Step 2a : If SA(A)+1 does not exist, assign B SA(A)+1
    //Step 2b : If SA(A)+1 exists, copy the stream starting at S(A)+1
    //    and then assign B SA(A)+1
//    cout << "-----S(A) : " << hex << str_addr_A << endl;
#ifdef DEBUG
    debug_cout << "-----S(A) : " << hex << str_addr_A << endl;
#endif
    // If S(A) is at a stream boundary return, we don't need to worry about B
    // because it is as good as a stream start
    if ((str_addr_A+1) % STREAM_MAX_LENGTH == 0){
        if (!str_addr_B_exists){
            ++new_stream_endstream;
            str_addr_B = assign_structural_addr();
#ifdef OFF_CHIP_ONLY
            off_chip_corr_matrix.update(phy_addr_B, str_addr_B);
#else
            on_chip_corr_matrix.update(phy_addr_B, str_addr_B, true);
            if (!off_chip_writeback) {
                off_chip_corr_matrix.update(phy_addr_B, str_addr_B);
                #ifdef BLOOM_ISB
                  #ifdef BLOOM_ISB_TRAFFIC_DEBUG
                  //printf("Bloom add d: 0x%lx\n", phy_addr_B);
                  #endif
                if (get_bloom_capacity() != 0) {
                  //add_to_bloom_filter(phy_addr_B);
                }
                #endif
//                if (count_off_chip_write_traffic) {
//                    on_chip_corr_matrix.access_off_chip(phy_addr_B, str_addr_B, ISB_OCI_REQ_STORE);
//                }
                ++off_chip_corr_matrix.update_count;
            }
#endif
            stream_head++;
            new_addr++;
        }
        else
            same_addr++;

        return str_addr_B;
    }

    bool invalidated = false;

    if (str_addr_B_exists){
        if ((str_addr_B >> STREAM_MAX_LENGTH_BITS) ==
                ((str_addr_A + 1)>>STREAM_MAX_LENGTH_BITS)){
#ifdef DEBUG
            debug_cout << hex << phy_addr_B
                << " has a structural address of " << str_addr_B
                << " conf++ " << endl;
#endif
#ifdef OFF_CHIP_ONLY
            off_chip_corr_matrix.increase_confidence(phy_addr_B);
#else
//            off_chip_corr_matrix.increase_confidence(phy_addr_B);
            on_chip_corr_matrix.increase_confidence(phy_addr_B);
#endif
            same_addr++;
            return str_addr_B;
        }
        else{
#ifdef DEBUG
            debug_cout << phy_addr_B << " has a structural address of "
                << hex << str_addr_B << " conf-- "  << endl;
#endif
#ifdef OFF_CHIP_ONLY
            bool ret = off_chip_corr_matrix.lower_confidence(phy_addr_B);
#else
//            off_chip_corr_matrix.lower_confidence(phy_addr_B);
            bool ret = on_chip_corr_matrix.lower_confidence(phy_addr_B);
#endif
            if (ret) {
                same_addr++;
                return str_addr_A;
            }
#ifdef DEBUG
            debug_cout << "Invalidate " << endl;
#endif
#ifdef OFF_CHIP_ONLY
            off_chip_corr_matrix.invalidate(phy_addr_B, str_addr_B);
#else
            off_chip_corr_matrix.invalidate(phy_addr_B, str_addr_B); //TODO: Should this be here?
            on_chip_corr_matrix.invalidate(phy_addr_B, str_addr_B);
#endif
            invalidated = true;
            inval_count++;
            str_addr_B_exists = false;
        }
    }

    assert(!str_addr_B_exists);
    not_found++;

    //Handle stream divergence
    uint64_t phy_addr_Aplus1 = 0;
#ifdef OFF_CHIP_ONLY
    bool phy_addr_Aplus1_exists =
        off_chip_corr_matrix.get_physical_address(phy_addr_Aplus1,
        str_addr_A+1);
    bool phy_addr_Aplus1_exists_off_chip = phy_addr_Aplus1_exists;
#else
    bool phy_addr_Aplus1_exists =
        on_chip_corr_matrix.get_physical_address(phy_addr_Aplus1,
                str_addr_A+1, false);
    bool phy_addr_Aplus1_exists_off_chip =
        off_chip_corr_matrix.get_physical_address(phy_addr_Aplus1,
                str_addr_A+1);
#endif
    if (phy_addr_Aplus1_exists) {
        stream_divergence_count++;
    }
    if (phy_addr_Aplus1_exists_off_chip) {
        stream_divergence_count_offchip++;
    }
    assert(!phy_addr_Aplus1_exists || phy_addr_Aplus1 != 0);
    if (!phy_addr_Aplus1_exists && invalidated)
        no_conflict_count++;
//#define CFIX
#ifndef CFIX
    if (phy_addr_Aplus1_exists || phy_addr_Aplus1_exists_off_chip)
    {
        //Old solution: Nothing fancy, just assign a new address
        if (invalidated)
            return str_addr_B;
        else
        {
            ++new_stream_divergence;
            str_addr_B = assign_structural_addr();
#ifdef OFF_CHIP_ONLY
            off_chip_corr_matrix.update(phy_addr_B, str_addr_B);
#else
            on_chip_corr_matrix.update(phy_addr_B, str_addr_B, true);
            if (!off_chip_writeback) {
                off_chip_corr_matrix.update(phy_addr_B, str_addr_B);
                 #ifdef BLOOM_ISB
                   #ifdef BLOOM_ISB_TRAFFIC_DEBUG
                   //printf("Bloom add e: 0x%lx\n", phy_addr_B);
                   #endif
                 if (get_bloom_capacity() != 0) {
                   //add_to_bloom_filter(phy_addr_B);
                 }
                 #endif
//                if (count_off_chip_write_traffic) {
//                    on_chip_corr_matrix.access_off_chip(phy_addr_B, str_addr_B, ISB_OCI_REQ_STORE);
//                }
                ++off_chip_corr_matrix.update_count;
            }
#endif
            return str_addr_B;
        }
    }
    else
    {
        str_addr_B = str_addr_A + 1;
#ifdef OFF_CHIP_ONLY
        off_chip_corr_matrix.update(phy_addr_B, str_addr_B);
#else
        on_chip_corr_matrix.update(phy_addr_B, str_addr_B, true);
        if (!off_chip_writeback) {
            if (!off_chip_writeback) {
                off_chip_corr_matrix.update(phy_addr_B, str_addr_B);
                 #ifdef BLOOM_ISB
                   #ifdef BLOOM_ISB_TRAFFIC_DEBUG
                   //printf("Bloom add f: 0x%lx\n", phy_addr_B);
                   #endif
                 if (get_bloom_capacity() != 0) {
                   //add_to_bloom_filter(phy_addr_B);
                 }
                 #endif
//                if (count_off_chip_write_traffic) {
//                    on_chip_corr_matrix.access_off_chip(phy_addr_B, str_addr_B, ISB_OCI_REQ_STORE);
//                }
                ++off_chip_corr_matrix.update_count;
            }
        }
#endif
    }
#else
    unsigned i=1;
    while (phy_addr_Aplus1_exists){
        i++;
        if ((str_addr_A+i) % STREAM_MAX_LENGTH == 0)
        {
            ++new_stream_divergence;
            str_addr_B = assign_structural_addr();
            new_addr++;
            stream_head++;
            if (invalidated) {
                new_stream_count++;
            }
            break;
        }
#ifdef OFF_CHIP_ONLY
        phy_addr_Aplus1_exists =
            off_chip_corr_matrix.get_physical_address(phy_addr_Aplus1,
                    str_addr_A+i);
#else
        phy_addr_Aplus1_exists =
            on_chip_corr_matrix.get_physical_address(phy_addr_Aplus1,
                    str_addr_A+i, false);
#endif
        if (!phy_addr_Aplus1_exists && invalidated) {
                same_stream_count++;
        }

    }

    if (!phy_addr_Aplus1_exists)
    {
        new_addr++;
        str_addr_B = str_addr_A + i;
    }
#endif


#ifdef OFF_CHIP_ONLY
    off_chip_corr_matrix.update(phy_addr_B, str_addr_B);
#else
    on_chip_corr_matrix.update(phy_addr_B, str_addr_B, true);
    if (!off_chip_writeback) {
        off_chip_corr_matrix.update(phy_addr_B, str_addr_B);
        #ifdef BLOOM_ISB
          #ifdef BLOOM_ISB_TRAFFIC_DEBUG
          //printf("Bloom add g: 0x%lx\n", phy_addr_B);
          #endif
        if (get_bloom_capacity() != 0) {
          //add_to_bloom_filter(phy_addr_B);
        }
        #endif
//        if (count_off_chip_write_traffic) {
//            on_chip_corr_matrix.access_off_chip(phy_addr_B, str_addr_B, ISB_OCI_REQ_STORE);
//        }
        ++off_chip_corr_matrix.update_count;
    }

#ifdef DEBUG
    debug_cout << hex << phy_addr_B
        << " allotted a structural address of " << str_addr_B << endl;
    debug_cout << "-----S(B) : " << hex << str_addr_B  << endl;
#endif
#endif
    return str_addr_B;
}

uint32_t IsbPrefetcher::assign_structural_addr()
{
    alloc_counter += STREAM_MAX_LENGTH;
#ifdef DEBUG
    debug_cout << "  ALLOC " << hex << alloc_counter  << endl;
#endif
    return alloc_counter;
}

void IsbPrefetcher::isb_predict(uint64_t trigger_phy_addr,
        uint32_t trigger_str_addr)
{
    debug_cout << "*Trigger Str addr " << hex
        << trigger_str_addr  << endl;
    uint64_t candidate;
    uint64_t phy_addr = 0;

    //uint64_t lookahead = this->lookahead - degree + 1;
    //uint32_t lookahead = config.lookahead;
    unsigned int count = 0, i=0;
    //bool buffer_added = false;
    //while (count < degree)
    //for (uint32_t i=0; i<1; i++)
    //while (count == 0)
    //while (count < degree)
    while (i < degree)
    {
        uint64_t str_addr_candidate = trigger_str_addr+lookahead+i;
        if (str_addr_candidate % STREAM_MAX_LENGTH == 0)
        {
            predict_stream_end++;
            return;
        }
#ifdef OFF_CHIP_ONLY
        bool ret = off_chip_corr_matrix.get_physical_address(phy_addr,
            str_addr_candidate);
        if (ret)
            ++off_chip_phy_found;
        if (ret)
#else
        bool ret = on_chip_corr_matrix.get_physical_address(phy_addr,
                str_addr_candidate, true);
        bool off_chip_ret = false;
        if (i==0) {
            if (ret) {
                ++on_chip_phy_found;
            } else {
                off_chip_ret = off_chip_corr_matrix.get_physical_address(phy_addr,
                    str_addr_candidate);
                if (off_chip_ret) {
                    ++off_chip_phy_found;
                } 
            }
        }


#ifdef IDEAL
        if (ret || off_chip_ret)
#else
        if (ret)
#endif
#endif
        {
            debug_cout << "*Predict Str addr "
                << hex << str_addr_candidate << ", Phy addr: " << phy_addr << dec << endl;
            assert(phy_addr != 0);
            candidate = phy_addr;
           if ( InitPrefetch(candidate, 2*i) )
                predictions++;
            count++;
        }
        else
        {
            debug_cout << "Adding pref_buffer for: " << hex << str_addr_candidate << endl;
            prefetch_buffer.add(str_addr_candidate);
            pf_buffer_add++;
        }
        if (!ret) {
#ifndef OFF_CHIP_ONLY
            if ((on_chip_corr_matrix.repl_policy == ISB_REPL_TYPE_METAPREF
                || on_chip_corr_matrix.repl_policy == ISB_REPL_TYPE_TLBSYNC_METAPREF)) {
                on_chip_corr_matrix.doPrefetch(trigger_phy_addr, trigger_str_addr, false);
            } else if ((on_chip_corr_matrix.repl_policy == ISB_REPL_TYPE_BULKMETAPREF
                || on_chip_corr_matrix.repl_policy == ISB_REPL_TYPE_TLBSYNC_BULKMETAPREF)) {
                on_chip_corr_matrix.doPrefetch(trigger_phy_addr, trigger_str_addr, false);
        //        on_chip_corr_matrix.doPrefetchBulk(addr_B, str_addr_B, false);
            }
#endif
        }
        i++;
#ifndef OFF_CHIP_ONLY
        if (ret || off_chip_ret) {
        //cout << "Metadata prefetch " << on_chip_corr_matrix.repl_policy << endl;
            if ((on_chip_corr_matrix.repl_policy == ISB_REPL_TYPE_METAPREF
                || on_chip_corr_matrix.repl_policy == ISB_REPL_TYPE_TLBSYNC_METAPREF)) {
                on_chip_corr_matrix.doPrefetch(phy_addr, str_addr_candidate, true);
            } else if ((on_chip_corr_matrix.repl_policy == ISB_REPL_TYPE_BULKMETAPREF
                || on_chip_corr_matrix.repl_policy == ISB_REPL_TYPE_TLBSYNC_BULKMETAPREF)) {
                on_chip_corr_matrix.doPrefetch(phy_addr, str_addr_candidate, true);
//                on_chip_corr_matrix.doPrefetchBulk(phy_addr, str_addr_candidate, true);
            }
        }
#endif
    }

}

IsbPrefetcher::IsbPrefetcher(const pf_isb_conf_t *p):
#ifndef OFF_CHIP_ONLY
      on_chip_corr_matrix(p, &off_chip_corr_matrix, this),
#endif
      lookahead(p->lookahead),
      degree(p->degree)
{
    alloc_counter = STREAM_MAX_LENGTH;
    reset_stats();
}

bool IsbPrefetcher::issue_prefetch_buffer()
{   
    bool prefetch_issued = false;
    for(uint32_t i=0; i<prefetch_buffer_size; i++)
    {
        uint64_t phy_addr;
        if(!prefetch_buffer.valid[i])
            continue;
        bool ret = on_chip_corr_matrix.get_physical_address(phy_addr, prefetch_buffer.buffer[i], false);
        if(ret) {
            uint64_t candidate = phy_addr;
            InitPrefetch(candidate);
            prefetch_buffer.issue(i);
            prefetch_issued = true;
            pf_buffer_issue++;
            debug_cout << "Issuing PF Buffer for: " << hex <<  prefetch_buffer.buffer[i] << ", ret = " << ret << endl;
        }
    }

    return prefetch_issued;
}

// Arguments passed are virtual page tags so the last 12 bits have already been dropped
// Way is a unqiue ID assigned to each TLB entry; if the the TLB is not fully associaive, way 
// should be combination of setID and wayID
// XXX both page size and cache block size are hard-coded.
vector<uint64_t> IsbPrefetcher::informTLBEviction(uint64_t inserted_vaddr, uint64_t evicted_vaddr)
{
    prefetch_list.clear();

    // Only do TLBsync if ISB has an on-chip metadata storage
#ifndef OFF_CHIP_ONLY
    // If ON_CHIP_REPL is not TLB SYNC we don't do anything here.
    if (on_chip_corr_matrix.repl_policy != ISB_REPL_TYPE_TLBSYNC
            && on_chip_corr_matrix.repl_policy != ISB_REPL_TYPE_TLBSYNC_METAPREF
            && on_chip_corr_matrix.repl_policy != ISB_REPL_TYPE_TLBSYNC_BULKMETAPREF
            ) {
        return prefetch_list;
    }

    debug_cout << hex << "TLB Eviction: " << evicted_vaddr << " " << inserted_vaddr << endl;

    uint64_t inserted_page_addr = inserted_vaddr >> 12;
    uint64_t evicted_page_addr = evicted_vaddr >> 12;

    debug_cout << hex << "TLB Eviction Pageaddr: " << evicted_page_addr << " " << inserted_page_addr << endl;
    if (evicted_page_addr == inserted_page_addr) {
        debug_cout << "Inserted page addr is the same as evicted page: " << inserted_page_addr << endl;;
        return prefetch_list;
    }

    if(inserted_page_addr == last_page) {
        debug_cout << "Inserted page addr is the same as last page: " << inserted_page_addr << endl;;
        return prefetch_list;
    }

    for(unsigned i=0; i<64; i++)
    {
        uint64_t phy_addr_to_evict = (evicted_page_addr<<12) | (i<<6);
        on_chip_corr_matrix.mark_not_tlb_resident(phy_addr_to_evict);
        debug_cout << " TLB Evicting: " << hex << evicted_page_addr << " " << phy_addr_to_evict << endl;
    }
    //InitMetadataWrite((evicted_page_addr ^ 0xabcdabcdab)  << 6);

    for(unsigned i=0; i<64; i++){
        uint64_t phy_addr_to_fetch = (inserted_page_addr << 12) | (i << 6);
        uint32_t str_addr_to_fetch;
        ++tlbsync_fetch_total;
        if (!on_chip_corr_matrix.get_structural_address(phy_addr_to_fetch, str_addr_to_fetch, false) )
        {
            debug_cout << " TLB Inserting: " << hex <<  phy_addr_to_fetch << " " << str_addr_to_fetch << endl;
            ++tlbsync_fetch_actual;
            if (on_chip_corr_matrix.ideal_off_chip_transaction) {
                on_chip_corr_matrix.update(phy_addr_to_fetch, str_addr_to_fetch, true);
            } else {
                debug_cout << " TLB Sync: " << hex <<  phy_addr_to_fetch << " " << str_addr_to_fetch << endl;
//                on_chip_corr_matrix.access_off_chip(phy_addr_to_fetch, str_addr_to_fetch, ISB_OCI_REQ_LOAD_PS);
            }
            on_chip_corr_matrix.mark_tlb_resident(phy_addr_to_fetch);
        }
    }
    //InitMetadataRead((inserted_page_addr ^ 0xabcdabcdab)  << 6);

    last_page = inserted_page_addr;

    issue_prefetch_buffer();

#if 0
    for(uint32_t i=0; i<BUFFER_SIZE; i++)
    {
        uint64_t phy_addr;
        if(!prefetch_buffer.valid[i])
            continue;
        bool ret = on_chip_corr_matrix.get_physical_address(phy_addr, prefetch_buffer.buffer[i]);
        if(ret){
            uint64_t candidate = phy_addr;
            if ((candidate >> 12) != inserted_page_addr)
                continue;
            InitPrefetch(candidate);
            prefetch_buffer.issue(i);
        }
    }
#endif

#endif
    return prefetch_list;
}

void IsbPrefetcher::set_conf(const pf_isb_conf_t *p)
{
#ifndef OFF_CHIP_ONLY
    on_chip_corr_matrix.set_conf(p, this);
#endif
    log_cacheblocksize = p->log_cacheblocksize;
    prefetch_buffer_size = p->prefetch_buffer_size;
    lookahead = p->lookahead;
    degree = p->degree;
    off_chip_writeback = p->isb_off_chip_writeback;
    count_off_chip_write_traffic = p->count_off_chip_write_traffic;

    #ifdef BLOOM_ISB
    set_bloom_capacity(p->bloom_capacity);
    set_bloom_region_shift_bits(p->bloom_region_shift_bits);
    set_bloom_fprate(p->bloom_fprate);
    allocate_bloom_filter();
    #endif
}

void IsbPrefetcher::calculatePrefetch(uint64_t addr_B, uint64_t pc, bool hit,
        uint64_t* pref_addresses, int pref_addresses_size)
{
    if (pc == 0) return; //TODO: think on how to handle prefetches from lower level

    debug_cout << hex << addr_B << ", " << pc << ", " << dec
        << on_chip_corr_matrix.get_ps_size() << ", "
        << on_chip_corr_matrix.get_sp_size() << ", "
        << off_chip_corr_matrix.get_ps_size() << ", "
        << off_chip_corr_matrix.get_sp_size()
        << endl;

    triggers++;
    prefetch_list.clear();
    addr_B = (addr_B>>log_cacheblocksize) << log_cacheblocksize;
#ifdef DEBUG
    debug_cout << "Trigger with " << hex << pc << " for addr " << addr_B
        << endl;
#endif
    uint32_t str_addr_B = 0;

#ifdef OFF_CHIP_ONLY
    bool str_addr_B_exists =
        off_chip_corr_matrix.get_structural_address(addr_B, str_addr_B);
    bool str_addr_B_exists_off_chip = str_addr_B_exists;
    if (str_addr_B_exists)
        ++off_chip_str_found;
#else
    bool str_addr_B_exists =
        on_chip_corr_matrix.get_structural_address(addr_B, str_addr_B, true);
    bool str_addr_B_exists_off_chip = false;
    if (str_addr_B_exists)
        ++on_chip_str_found;
    else {
        str_addr_B_exists_off_chip =
            off_chip_corr_matrix.get_structural_address(addr_B, str_addr_B);

        #ifdef BLOOM_ISB
        if (get_bloom_capacity() != 0) {
           /*
            * Algorithm:
            *  For every write to off_chip (writeback) update the bloom filter
            *  do the bloom filter look up here
            *  Initially cross-check the boom filter output with str_add_B_exists_off_chip value below
           */
           bool str_addr_B_exists_off_chip_bloom = false;
           str_addr_B_exists_off_chip_bloom = lookup_bloom_filter(addr_B);
           if (str_addr_B_exists_off_chip_bloom) ++ps_bloom_found; else ++ps_bloom_not_found;
           if (str_addr_B_exists_off_chip) ++ps_bloom_wb_found; else ++ps_bloom_wb_not_found;
           if (str_addr_B_exists_off_chip) {
             if (str_addr_B_exists_off_chip == str_addr_B_exists_off_chip_bloom) ++ps_offchip_bloom_correct;
             else ++ps_offchip_bloom_incorrect;
           } else {
             if (str_addr_B_exists_off_chip == str_addr_B_exists_off_chip_bloom) ++ps_not_offchip_bloom_correct;
             else ++ps_not_offchip_bloom_incorrect;
           }
           #ifdef BLOOM_ISB_TRACE
           printf("BLTRACE: addr 0x%lx, GroundTruth %d, BloomSays: %d\n", addr_B, str_addr_B_exists_off_chip, str_addr_B_exists_off_chip_bloom);
           #endif
           #ifdef BLOOM_ISB_TRAFFIC_DEBUG
           printf("Bloom lookup: 0x%lx, found? %d, match? %d\n", addr_B, str_addr_B_exists_off_chip_bloom, (str_addr_B_exists_off_chip == str_addr_B_exists_off_chip_bloom));
           #endif
        }
        #endif
        if (str_addr_B_exists_off_chip)
            ++off_chip_str_found;
        else
            str_addr_B = INVALID_ADDR;
    }

#ifdef SHOW_DETAIL
    if (str_addr_B_exists) {
        cout << "SF 1 " << hex << addr_B << ' ' << str_addr_B << ' '
            << pc << endl;
        debug_cout << "[ISB] On-chip phy found: phy_addr=" << hex << addr_B
            << " str_addr=" << str_addr_B << ", pc=" << pc << endl;
    } else if (str_addr_B_exists_off_chip) {
        cout << "SF 2 " << hex << addr_B << ' ' << str_addr_B << ' '
            << pc << endl;
        debug_cout << "[ISB] Off-chip phy found: phy_addr=" << hex << addr_B
            << " str_addr=" << str_addr_B << ", pc=" << pc << endl;
    } else {
        cout << "SF 3 " << hex << addr_B << ' ' << 0 << ' '
            << pc << endl;
        debug_cout << "[ISB] Off-chip phy miss: phy_addr=" << hex << addr_B << ", pc=" << pc << endl;
    }
#endif

    if (!str_addr_B_exists) {
        if (on_chip_corr_matrix.repl_policy == ISB_REPL_TYPE_BULKLRU
                || on_chip_corr_matrix.repl_policy == ISB_REPL_TYPE_BULKMETAPREF) {
//            on_chip_corr_matrix.fetch_bulk(addr_B, ISB_OCI_REQ_LOAD_PS);
            on_chip_corr_matrix.access_off_chip(addr_B, str_addr_B, ISB_OCI_REQ_LOAD_PS);
        }
    }
#endif

    // Stats: Count Trigger Count
#ifdef OFF_CHIP_ONLY
    if (!hit && (str_addr_B_exists || str_addr_B_exists_off_chip)) {
        trigger_addr_count[addr_B]++;
    }
#else
#ifdef COUNT_STREAM_DETAIL
    if ((str_addr_B_exists || str_addr_B_exists_off_chip)
            && (on_chip_corr_matrix.is_trigger_access(str_addr_B))) {
        trigger_addr_count[addr_B]++;
    }
#endif
#endif

    // Prediction Starts
#ifdef PS_FIX_PREDICTION
    if (str_addr_B_exists || str_addr_B_exists_off_chip) {
        predict_init++;
        isb_predict(addr_B, str_addr_B);
    }
#else
    if (str_addr_B_exists){
        predict_init++;
        isb_predict(addr_B, str_addr_B);
    }
#endif

    // Prediction Ends
    //
    // Training Starts
    //if(!str_addr_B_exists && str_addr_B_exists_off_chip)
     //   cout <<  "Missing PS mapping on-chip " << hex << str_addr_B << dec << endl;
#ifdef PS_FIX
    str_addr_B_exists = str_addr_B_exists || str_addr_B_exists_off_chip;
#endif
    isb_train_addr(pc, str_addr_B_exists, addr_B, str_addr_B);

    // Training Ends

    // Add the prefetch list to addresses
    for (size_t i = 0; i < prefetch_list.size() && i < pref_addresses_size; ++i) {
        pref_addresses[i] = prefetch_list[i];
    }

    return;
}

/*
void IsbPrefetcher::handle_metadata_state(control_t* data)
{
#ifndef OFF_CHIP_ONLY
    on_chip_corr_matrix.oci_filler_impl(data);
#endif
}
*/

void IsbPrefetcher::reset_stats()
{
    // Initialize Internal Stats Collection
    triggers = 0; // Number of trigger access (e.q L2 cache access in current experiments) to ISB
    stream_head = 0; // Number of times when an access is creating a new stream
    same_addr = 0; // Number of times when we retain the old structural address to an access
    new_addr = 0; // Number of times when we assign a new structural address to an access
    stream_divergence_count = 0; // Number of times when the structure address is already occupied.
    stream_divergence_count_offchip = 0; // Number of times when the structure address is already occupied.
    not_found = 0; // Number of times when access cannot be found in on-chip data

    inval_count = 0; // Number of times a structure-physical mapping is invalidated due to confidence reaches 0
    no_conflict_count = 0; // Number of times when an address happens somewhere else in the same stream
    same_stream_count = 0; // In CFIX, how many times old straddr and new straddr belong to the same stream
    new_stream_count = 0; // In CFIX, how many times we generate a new stream for an aliasing access

    repeat = 0; // Number of times an access is the same cache line with the previous access in the stream

    // Prediction Stats
    predictions = 0; // Number of prefetches generated by ISB (including redundant etc.)
    predict_init = 0; // Number of times a trigger access has a structural address in prediction phase
    predict_stream_end = 0; // Number of times a prediction phase reaches max stream length (256 by default)
    //predict_oci_miss = 0;
    //on_chip_mispredictions = 0;

    // On-Chip v.s. Off-Chip Stats
    //    off_chip_phy_request = 0;
    //    off_chip_str_request = 0;
    on_chip_phy_found = 0;
    off_chip_phy_found = 0;
    on_chip_str_found = 0;
    off_chip_str_found = 0;

    pf_buffer_add = 0;
    pf_buffer_issue = 0;

    tlbsync_fetch_total = 0;
    tlbsync_fetch_actual = 0;

    new_stream_new_pc_addr = 0;
    new_stream_endstream = 0;
    new_stream_divergence = 0;

    stream_count = 0;
    stream_length_count = 0;
    stream_trigger_count = 0;
    stream_region_count = 0;
    stream_agg_region_count = 0;
    #ifdef BLOOM_ISB
    ps_offchip_bloom_incorrect = 0;
    ps_offchip_bloom_correct = 0;
    ps_not_offchip_bloom_correct = 0;
    ps_not_offchip_bloom_incorrect = 0;
    ps_bloom_wb_found = 0;
    ps_bloom_wb_not_found = 0;
    ps_bloom_found = 0;
    ps_bloom_not_found = 0;
    #endif

    ps_md_requests = 0;
    sp_md_requests = 0;
    write_md_requests = 0;
}

void IsbPrefetcher::read_metadata(uint64_t addr, uint64_t phy_addr, uint32_t str_addr, off_chip_req_type_t req_type)
{
    static const unsigned long long crcPolynomial = 3988292384ULL;
    uint64_t meta_data_addr = (addr)^crcPolynomial;
    //for( unsigned int i = 0; i < 32; i++ )
     //   meta_data_addr = ( ( meta_data_addr & 1 ) == 1 ) ? ( ( meta_data_addr >> 1 ) ^ crcPolynomial ) : ( meta_data_addr >> 1 );

    assert(req_type != ISB_OCI_REQ_STORE);

    if(metadata_mapping.find(meta_data_addr) != metadata_mapping.end())
        return;

    if(req_type == ISB_OCI_REQ_LOAD_PS)
    {
        if(ideal_bloom_filter.find(meta_data_addr) == ideal_bloom_filter.end())
            return;
        ps_md_requests++;
        metadata_mapping[meta_data_addr].set(phy_addr, str_addr, true);
    }
    else if(req_type == ISB_OCI_REQ_LOAD_SP1 || req_type == ISB_OCI_REQ_LOAD_SP2)
    {
        sp_md_requests++;
        metadata_mapping[meta_data_addr].set(phy_addr, str_addr, false);
    //    cout << "Sending out " << hex << meta_data_addr << " " << dec << (void*)str_addr << endl;
    }

    metadata_read_requests.insert(meta_data_addr);
    //cout << "Read MD " << hex << meta_data_addr << dec << endl;
    //cout << "             " << hex << phy_addr << " " << str_addr << " " << dec << (uint32_t)req_type << endl;
    //complete_metadata_req(meta_data_addr);
}

void IsbPrefetcher::write_metadata(uint64_t addr, off_chip_req_type_t req_type)
{
    assert(req_type == ISB_OCI_REQ_STORE);
    static const unsigned long long crcPolynomial = 3988292384ULL;
    unsigned long long meta_data_addr = (addr)^crcPolynomial;
    //for( unsigned int i = 0; i < 32; i++ )
    //    meta_data_addr = ( ( meta_data_addr & 1 ) == 1 ) ? ( ( meta_data_addr >> 1 ) ^ crcPolynomial ) : ( meta_data_addr >> 1 );

    write_md_requests++;
    metadata_write_requests.insert(meta_data_addr);
    ideal_bloom_filter.insert(meta_data_addr);
//    cout << "Write " << hex << meta_data_addr << dec << endl;
}

void IsbPrefetcher::complete_metadata_req(uint64_t meta_data_addr)
{
    //cout << "Complete MD: " << hex << meta_data_addr << dec << endl;
    //assert(metadata_mapping.find(meta_data_addr) != metadata_mapping.end());
    if(metadata_mapping.find(meta_data_addr) == metadata_mapping.end())
        return;

    if(metadata_mapping[meta_data_addr].ps) {
        on_chip_corr_matrix.insert_metadata_ps(metadata_mapping[meta_data_addr].phy_addr);
        for(auto it=metadata_mapping[meta_data_addr].phy_addrs.begin(); it != metadata_mapping[meta_data_addr].phy_addrs.end(); it++)
        {
            uint32_t str_addr;
            bool ret = off_chip_corr_matrix.get_structural_address(*it, str_addr);
            if (ret && (str_addr != INVALID_ADDR))
                isb_predict(*it, str_addr);
        }
    }
    else
    {
        //cout << "Erasing: " << (void*)metadata_mapping[meta_data_addr].str_addr << endl;
        on_chip_corr_matrix.insert_metadata_sp(metadata_mapping[meta_data_addr].str_addr);
    }

    metadata_mapping.erase(meta_data_addr);
}


#define CSV_STATT(base, name, value) printf("%s_%s=%lu\n", base, name, value)
void IsbPrefetcher::dump_stats()
{
    char base[256];
    sprintf(base, "ISB");
    //const int base_end = strlen(base);
    CSV_STATT(base, "nb_triggers", triggers);
    CSV_STATT(base, "nb_stream_head", stream_head);
    CSV_STATT(base, "nb_same_addr", same_addr);
    CSV_STATT(base, "nb_new_addr", new_addr);
    CSV_STATT(base, "nb_stream_divergence_count", stream_divergence_count);
    CSV_STATT(base, "nb_stream_divergence_count_offchip", stream_divergence_count_offchip);
    CSV_STATT(base, "nb_not_found", not_found);
    CSV_STATT(base, "nb_inval_count", inval_count);
    CSV_STATT(base, "nb_no_conflict_count", no_conflict_count);
    CSV_STATT(base, "nb_same_stream_count", same_stream_count);
    CSV_STATT(base, "nb_new_stream_count", new_stream_count);
    CSV_STATT(base, "nb_repeat", repeat);
    CSV_STATT(base, "nb_predictions", predictions);
    CSV_STATT(base, "nb_predict_init", predict_init);
    CSV_STATT(base, "nb_predict_stream_end", predict_stream_end);
    //CSV_STATT(base, "nb_on_chip_mispredictions", on_chip_mispredictions);
    CSV_STATT(base, "nb_on_chip_phy_found", on_chip_phy_found);
    CSV_STATT(base, "nb_off_chip_phy_found", off_chip_phy_found);
    CSV_STATT(base, "nb_on_chip_str_found", on_chip_str_found);
    CSV_STATT(base, "nb_off_chip_str_found", off_chip_str_found);
    CSV_STATT(base, "nb_tlbsync_fetch_total", tlbsync_fetch_total);
    CSV_STATT(base, "nb_tlbsync_fetch_actual", tlbsync_fetch_actual);
    CSV_STATT(base, "nb_bulk_actual", on_chip_corr_matrix.bulk_actual);
    CSV_STATT(base, "nb_training_pc_found", training_unit.training_unit_pc_found);
    CSV_STATT(base, "nb_training_pc_not_found", training_unit.training_unit_pc_not_found);
    // Off chip traffic
    CSV_STATT(base, "nb_off_chip_phy_access", off_chip_corr_matrix.phy_access_count);
    CSV_STATT(base, "nb_off_chip_phy_success", off_chip_corr_matrix.phy_success_count);
    CSV_STATT(base, "nb_off_chip_str_access", off_chip_corr_matrix.str_access_count);
    CSV_STATT(base, "nb_off_chip_str_success", off_chip_corr_matrix.str_success_count);
    CSV_STATT(base, "nb_off_chip_update", off_chip_corr_matrix.update_count);
    CSV_STATT(base, "nb_prefetch_buffer_add", pf_buffer_add);
    CSV_STATT(base, "nb_prefetch_buffer_issue", pf_buffer_issue);
    // On chip stats
#ifndef OFF_CHIP_ONLY
#ifdef BLOOM_ISB
    // ks stats
    CSV_STATT(base, "ps_offchip_bloom_correct", ps_offchip_bloom_correct);
    CSV_STATT(base, "ps_offchip_bloom_incorrect", ps_offchip_bloom_incorrect);
    CSV_STATT(base, "ps_not_offchip_bloom_correct", ps_not_offchip_bloom_correct);
    CSV_STATT(base, "ps_not_offchip_bloom_incorrect", ps_not_offchip_bloom_incorrect);
    CSV_STATT(base, "ps_bloom_wb_found", ps_bloom_wb_found);
    CSV_STATT(base, "ps_bloom_wb_not_found", ps_bloom_wb_not_found);
    CSV_STATT(base, "ps_bloom_found", ps_bloom_found);
    CSV_STATT(base, "ps_bloom_not_found", ps_bloom_not_found);
#endif
    CSV_STATT(base, "nb_on_chip_ps_accesses", on_chip_corr_matrix.ps_accesses);
    CSV_STATT(base, "nb_on_chip_ps_hits", on_chip_corr_matrix.ps_hits);
    CSV_STATT(base, "nb_on_chip_ps_prefetch_hits", on_chip_corr_matrix.ps_prefetch_hits);
    CSV_STATT(base, "nb_on_chip_sp_accesses", on_chip_corr_matrix.sp_accesses);
    CSV_STATT(base, "nb_on_chip_sp_hits", on_chip_corr_matrix.sp_hits);
    CSV_STATT(base, "nb_on_chip_sp_prefetch_hits", on_chip_corr_matrix.sp_prefetch_hits);
    CSV_STATT(base, "nb_on_chip_sp_not_found", on_chip_corr_matrix.sp_not_found);
    CSV_STATT(base, "nb_on_chip_sp_invalid", on_chip_corr_matrix.sp_invalid);
    CSV_STATT(base, "nb_on_chip_metapref_count", on_chip_corr_matrix.metapref_count);
    CSV_STATT(base, "nb_on_chip_metapref_duplicate", on_chip_corr_matrix.metapref_duplicate);
    CSV_STATT(base, "nb_on_chip_metapref_conflict", on_chip_corr_matrix.metapref_conflict);
    CSV_STATT(base, "nb_on_chip_metapref_actual", on_chip_corr_matrix.metapref_actual);
    CSV_STATT(base, "nb_on_chip_metapref_success", on_chip_corr_matrix.oci_pref.metapref_success);
    CSV_STATT(base, "nb_on_chip_metapref_not_found", on_chip_corr_matrix.oci_pref.metapref_not_found);
    CSV_STATT(base, "nb_on_chip_metapref_stream_end", on_chip_corr_matrix.oci_pref.metapref_stream_end);
    CSV_STATT(base, "nb_on_chip_filler_full_count", on_chip_corr_matrix.filler_full_count);
    CSV_STATT(base, "nb_on_chip_filler_same_addr", on_chip_corr_matrix.filler_same_addr);
    CSV_STATT(base, "nb_on_chip_filler_load_count", on_chip_corr_matrix.filler_load_count);
    CSV_STATT(base, "nb_on_chip_filler_load_ps_count", on_chip_corr_matrix.filler_load_ps_count);
    CSV_STATT(base, "nb_on_chip_filler_load_sp1_count", on_chip_corr_matrix.filler_load_sp1_count);
    CSV_STATT(base, "nb_on_chip_filler_load_sp2_count", on_chip_corr_matrix.filler_load_sp2_count);
    CSV_STATT(base, "nb_on_chip_filler_store_count", on_chip_corr_matrix.filler_store_count);
    CSV_STATT(base, "nb_on_chip_bandwidth_delay_cycles", on_chip_corr_matrix.bandwidth_delay_cycles);
    CSV_STATT(base, "nb_on_chip_issue_delay_cycles", on_chip_corr_matrix.issue_delay_cycles);
#endif

    // Stream stats
    CSV_STATT(base, "stream_count", stream_count);
    CSV_STATT(base, "stream_length_count", stream_length_count);
    CSV_STATT(base, "stream_trigger_count", stream_trigger_count);
    CSV_STATT(base, "stream_trigger_region_count", stream_trigger_region_count);
    CSV_STATT(base, "stream_region_count", stream_region_count);
    CSV_STATT(base, "aggregated_region_count", stream_agg_region_count);

    CSV_STATT(base, "PS-Req", ps_md_requests);
    CSV_STATT(base, "SP-Req", sp_md_requests);
    CSV_STATT(base, "Write-Req", write_md_requests);

}

/*
void IsbPrefetcher::print_detailed_stats(const prefetcher_t *pf, tprinter_t *tp)
{
#define BS 256
    //  char tmp[BS];
    int sidx = -1;

    TRACE("Detailed stats for %s:\n", pf->name);
    table_printer_reset(tp);
    table_printer_show_outline(tp, true);
    table_printer_set_first_column_is_not_label(tp);

    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_triggers", BS, "%ld", triggers);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_stream_head", BS, "%ld", stream_head);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_same_addr", BS, "%ld", same_addr);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_new_addr", BS, "%ld", new_addr);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_stream_divergence_count", BS, "%ld", stream_divergence_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_stream_divergence_count_offchip", BS, "%ld", stream_divergence_count_offchip);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_not_found", BS, "%ld", not_found);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_inval_count", BS, "%ld", inval_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_no_conflict_count", BS, "%ld", no_conflict_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_same_stream_count", BS, "%ld", same_stream_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_new_stream_count", BS, "%ld", new_stream_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_repeat", BS, "%ld", repeat);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_predictions", BS, "%ld", predictions);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_predict_init", BS, "%ld", predict_init);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_predict_stream_end", BS, "%ld", predict_stream_end);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_phy_found", BS, "%ld", on_chip_phy_found);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_off_chip_phy_found", BS, "%ld", off_chip_phy_found);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_str_found", BS, "%ld", on_chip_str_found);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_off_chip_str_found", BS, "%ld", off_chip_str_found);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_tlbsync_fetch_total", BS, "%ld", tlbsync_fetch_total);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_tlbsync_fetch_actual", BS, "%ld", tlbsync_fetch_actual);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_bulk_actual", BS, "%ld", on_chip_corr_matrix.bulk_actual);
    //sidx = table_printer_add_column(tp);
    //sidx = table_printer_add_column(tp);
    //ADD_STAT(sidx, sidx, "nb_on_chip_mispredictions", BS, "%ld", on_chip_mispredictions);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_new_stream_newpcaddr", BS, "%ld", new_stream_new_pc_addr);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_new_stream_endstream", BS, "%ld", new_stream_endstream);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_new_stream_divergence", BS, "%ld", new_stream_divergence);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_training_pc_found", BS, "%ld", training_unit.training_unit_pc_found);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_training_pc_not_found", BS, "%ld", training_unit.training_unit_pc_not_found);
    //sidx = table_printer_add_column(tp);

    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_off_chip_str_access", BS, "%ld", off_chip_corr_matrix.str_access_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_off_chip_str_success", BS, "%ld", off_chip_corr_matrix.str_success_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_off_chip_phy_access", BS, "%ld", off_chip_corr_matrix.phy_access_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_off_chip_phy_success", BS, "%ld", off_chip_corr_matrix.phy_success_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_off_chip_update", BS, "%ld", off_chip_corr_matrix.update_count);

#ifndef OFF_CHIP_ONLY
    // on-chip stats
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_ps_accesses", BS, "%ld", on_chip_corr_matrix.ps_accesses);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_ps_hits", BS, "%ld", on_chip_corr_matrix.ps_hits);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_ps_prefetch_hits", BS, "%ld", on_chip_corr_matrix.ps_prefetch_hits);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_sp_accesses", BS, "%ld", on_chip_corr_matrix.sp_accesses);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_sp_hits", BS, "%ld", on_chip_corr_matrix.sp_hits);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_sp_prefetch_hits", BS, "%ld", on_chip_corr_matrix.sp_prefetch_hits);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_sp_not_found", BS, "%ld", on_chip_corr_matrix.sp_not_found);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_sp_invalid", BS, "%ld", on_chip_corr_matrix.sp_invalid);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_metapref_count", BS, "%ld", on_chip_corr_matrix.metapref_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_metapref_success", BS, "%ld", on_chip_corr_matrix.oci_pref.metapref_success);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_metapref_not_found", BS, "%ld", on_chip_corr_matrix.oci_pref.metapref_not_found);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_metapref_stream_end", BS, "%ld", on_chip_corr_matrix.oci_pref.metapref_stream_end);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_metapref_duplicate", BS, "%ld", on_chip_corr_matrix.metapref_duplicate);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_metapref_conflict", BS, "%ld", on_chip_corr_matrix.metapref_conflict);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_metapref_actual", BS, "%ld", on_chip_corr_matrix.metapref_actual);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_filler_full_count", BS, "%ld", on_chip_corr_matrix.filler_full_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_filler_same_addr", BS, "%ld", on_chip_corr_matrix.filler_same_addr);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_filler_load_count", BS, "%ld", on_chip_corr_matrix.filler_load_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_filler_load_ps_count", BS, "%ld", on_chip_corr_matrix.filler_load_ps_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_filler_load_sp1_count", BS, "%ld", on_chip_corr_matrix.filler_load_sp1_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_filler_load_sp2_count", BS, "%ld", on_chip_corr_matrix.filler_load_sp2_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_filler_store_count", BS, "%ld", on_chip_corr_matrix.filler_store_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_bandwidth_delay_cycles", BS, "%ld", on_chip_corr_matrix.bandwidth_delay_cycles);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "nb_on_chip_issue_delay_cycles", BS, "%ld", on_chip_corr_matrix.issue_delay_cycles);

  #ifdef BLOOM_ISB
    // ks stats
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "ps_offchip_bloom_correct", BS, "%ld", ps_offchip_bloom_correct);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "ps_offchip_bloom_incorrect", BS, "%ld", ps_offchip_bloom_incorrect);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "ps_not_offchip_bloom_correct", BS, "%ld", ps_not_offchip_bloom_correct);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "ps_not_offchip_bloom_incorrect", BS, "%ld", ps_not_offchip_bloom_incorrect);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "ps_bloom_wb_found", BS, "%ld", ps_bloom_wb_found);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "ps_bloom_wb_not_found", BS, "%ld", ps_bloom_wb_not_found);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "ps_bloom_found", BS, "%ld", ps_bloom_found);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "ps_bloom_not_found", BS, "%ld", ps_bloom_not_found);
  #endif

#endif


    // Stream stats
    count_stream_stats();
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "stream_count", BS, "%ld", stream_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "stream_length_count", BS, "%ld", stream_length_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "stream_trigger_count", BS, "%ld", stream_trigger_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "stream_trigger_region_count", BS, "%ld", stream_trigger_region_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "stream_region_count", BS, "%ld", stream_region_count);
    sidx = table_printer_add_column(tp);
    ADD_STAT(sidx, sidx, "aggregated_region_count", BS, "%ld", stream_agg_region_count);

    table_printer_print(tp, false);
    TRACE("\n");
#undef BS
}

void IsbPrefetcher::count_stream_stats()
{
    stream_count = 0;
    stream_length_count = 0;
    stream_trigger_count = 0;
    stream_region_count = 0;
    stream_agg_region_count = 0;

    uint32_t current_stream_length = 0;
    set<uint64_t> region_addr_set, agg_region_addr_set;
    uint64_t phy_addr, region_addr;
    for (uint64_t str_addr = 0; str_addr < alloc_counter + STREAM_MAX_LENGTH; ++str_addr) {
        if (unlikely(str_addr % STREAM_MAX_LENGTH == 0)) {
            ++stream_count;
            stream_length_count += current_stream_length;
            current_stream_length = 0;
            region_addr_set.clear();
        }
        if (off_chip_corr_matrix.get_physical_address(phy_addr, str_addr)) {
            ++current_stream_length;
            // XXX: Magic Number here for 4KB region size
            region_addr = phy_addr >> 12;

            if (!region_addr_set.count(region_addr)) {
                ++stream_region_count;
                region_addr_set.insert(region_addr);
            }

            if (!agg_region_addr_set.count(region_addr)) {
                ++stream_agg_region_count;
                agg_region_addr_set.insert(region_addr);
            }
        }
    }

    // count triggers
    region_addr_set.clear();
    stream_trigger_count = trigger_addr_count.size();
    for (map<uint64_t, uint64_t>::iterator it = trigger_addr_count.begin();
            it != trigger_addr_count.end(); ++it) {
        // XXX: Magic Number here for 4KB region size
        region_addr = (it->first) >> 12;
        if (!region_addr_set.count(region_addr)) {
            ++stream_trigger_region_count;
            region_addr_set.insert(region_addr);
        }
    }
}
*/

/* Interface starts here. */

const char *g_isb_repl_type_names[] = {
    [ISB_REPL_TYPE_LRU ] = "LRU",
    [ISB_REPL_TYPE_LFU ] = "LFU",
    [ISB_REPL_TYPE_BULKLRU ] = "BULKLRU",
    [ISB_REPL_TYPE_BULKMETAPREF ] = "BULKMETAPREF",
    [ISB_REPL_TYPE_TLBSYNC ] = "TLBSYNC",
    [ISB_REPL_TYPE_METAPREF ] = "METAPREF",
    [ISB_REPL_TYPE_TLBSYNC_METAPREF ] = "TLBSYNC_METAPREF",
    [ISB_REPL_TYPE_TLBSYNC_BULKMETAPREF ] = "TLBSYNC_BULKMETAPREF",
    [ISB_REPL_TYPE_PERFECT ] = "PERFECT",
    [ISB_REPL_TYPE_MAX ] = "INVALID"
};

