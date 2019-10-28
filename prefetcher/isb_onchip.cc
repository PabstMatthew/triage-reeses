
#include <cassert>
#include <climits>

#include <iostream>

#include "isb_onchip.h"
#include "isb.h"

using namespace std;

//#define OVERWRITE_OFF_CHIP

//#define DEBUG

#ifdef DEBUG
#define debug_cout std::cerr << "[ISBONCHIP] "
#else
#define debug_cout if (0) std::cerr
#endif

std::ostream* outf = &std::cerr;

OnChipInfo::OnChipInfo(const pf_isb_conf_t *p, OffChipInfo* off_chip_info, IsbPrefetcher* pref)
{
    this->off_chip_info = off_chip_info;
    this->pref = pref;
    off_chip_latency = p->isb_off_chip_latency;
    ideal_off_chip_transaction = p->isb_off_chip_ideal;
    off_chip_writeback = p->isb_off_chip_writeback;
    count_off_chip_write_traffic = p->count_off_chip_write_traffic;
    filler_count = p->isb_off_chip_fillers;
    oci_pref.init(off_chip_info, p->amc_metapref_degree);
#ifdef OVERWRITE_OFF_CHIP
    cout << "OVERWRITE_OFF_CHIP ON" << endl;;
#else
    cout << "OVERWRITE_OFF_CHIP OFF" << endl;;
#endif
}

void OnChipInfo::set_conf(const pf_isb_conf_t *p, IsbPrefetcher *pf)
{
    check_bandwidth = p->check_bandwidth;
    curr_timestamp = 0;
    repl_policy = p->repl_policy;
    amc_size = p->amc_size;
    amc_assoc = p->amc_assoc;
    regionsize = p->amc_repl_region_size;
    log_regionsize = p->amc_repl_log_region_size;
    log_cacheblocksize = p->log_cacheblocksize;
    num_sets = amc_size / amc_assoc;
    num_repl = num_sets;
    indexMask = num_sets-1;
    reset();

    off_chip_latency = p->isb_off_chip_latency;
    ideal_off_chip_transaction = p->isb_off_chip_ideal;
    filler_count = p->isb_off_chip_fillers;

    ps_accesses = 0, ps_hits = 0, ps_prefetch_hits = 0, ps_prefetch_count = 0;
    sp_accesses = 0, sp_hits = 0, sp_prefetch_hits = 0, ps_prefetch_count = 0;
    sp_not_found = 0; sp_invalid = 0;
    metapref_count = 0, metapref_conflict = 0, metapref_duplicate = 0, metapref_actual = 0;
    bulk_actual = 0;
    filler_full_count = 0;
    filler_same_addr = 0;
    filler_load_count = 0;
    filler_store_count = 0;

    bandwidth_delay_cycles = 0;
    issue_delay_cycles = 0;

    switch (repl_policy) {
        case ISB_REPL_TYPE_LRU:
        case ISB_REPL_TYPE_METAPREF:
        case ISB_REPL_TYPE_BULKLRU:
        case ISB_REPL_TYPE_BULKMETAPREF:
            repl_ps.resize(num_repl);
            repl_sp.resize(num_repl);
            for (unsigned i = 0; i < num_repl; ++i) {
                repl_ps[i] = new OnChipReplacementLRU;
                repl_sp[i] = new OnChipReplacementLRU;
            }
            break;
        case ISB_REPL_TYPE_LFU:
            repl_ps.resize(num_repl);
            repl_sp.resize(num_repl);
            for (unsigned i = 0; i < num_repl; ++i) {
                repl_ps[i] = new OnChipReplacementLFU;
                repl_sp[i] = new OnChipReplacementLFU;
            }
            break;
        case ISB_REPL_TYPE_TLBSYNC:
        case ISB_REPL_TYPE_TLBSYNC_METAPREF:
        case ISB_REPL_TYPE_TLBSYNC_BULKMETAPREF:
            break;
        case ISB_REPL_TYPE_PERFECT:
            break;
        default:
            // We have encountered a replacement policy which we don't support.
            // This should not happen. So we just panic quit.
            cerr << "Invalid ISB On Chip Replacement Policy " <<  repl_policy << endl;
    }

    /*
    for (uint32_t filler_id = 0; filler_id < filler_count; ++filler_id) {
        coroutine* self = &filler[filler_id];
        //cout << "Before coroutine init, pf: " << pf << endl; 
        //cout << " cache_ring: " << get_cache_ring(pf) << endl;
        coroutine_init(self, new control_t, "ISBMETAFILLER", pf_meta_handler, get_cache_ring(pf));
        self->ctl->owner = self;
        self->ctl->ptr_a = pf;
        self->ctl->ptr_b = self->ctl->ptr_c = 0;
        self->ctl->state = ISB_OCI_END;
        make_sleep(self);
    }
    */
}

void OnChipInfo::reset()
{
    ps_amc.resize(num_sets);
    sp_amc.resize(num_sets);
    for (unsigned int i=0; i<num_sets; i++)
    {
        ps_amc[i].clear();
        sp_amc[i].clear();
    }

}

size_t OnChipInfo::get_sp_size()
{
    size_t total_size = 0;

    for (size_t i = 0; i < sp_amc.size(); ++i) {
        total_size += sp_amc[i].size();
    }

    return total_size;
}

size_t OnChipInfo::get_ps_size()
{
    size_t total_size = 0;

    for (size_t i = 0; i < ps_amc.size(); ++i) {
        total_size += ps_amc[i].size();
    }

    return total_size;
}

bool OnChipInfo::get_structural_address(uint64_t phy_addr, uint32_t& str_addr, bool update_stats, bool clear_dirty)
{
    curr_timestamp++;
    unsigned int setId = (phy_addr >> 6) & indexMask;
    debug_cout << "get structural addr for addr " << (void*)phy_addr << ", set_id: " << setId
        << ", indexMask: " << indexMask << endl;
    std::map<uint64_t, OnChip_PS_Entry*>& ps_map = ps_amc[setId];
    std::map<uint64_t, OnChip_PS_Entry*>::iterator ps_iter = ps_map.find(phy_addr);
    debug_cout << "PS ACCESS: " << phy_addr << endl;
    if (update_stats) {
        ++ps_accesses;
    }
    if (ps_iter == ps_map.end()) {
#ifdef DEBUG
        (*outf)<<"In on-chip get_structural address of phy_addr "
            <<phy_addr<<", str addr not found\n";
#endif
        return false;
    }
    else if (ps_iter->second->valid) {
        str_addr = ps_iter->second->str_addr;
        ps_iter->second->last_access = curr_timestamp;
#ifdef DEBUG
        (*outf)<<"In on-chip get_structural address of phy_addr "
            <<phy_addr<<", str addr is "<<str_addr<<endl;
#endif
        if (update_stats) {
            ++ps_hits;
        }
        if (clear_dirty) {
            ps_iter->second->dirty = false;
        }
        return true;
    } else {
#ifdef DEBUG
        (*outf)<<"In on-chip get_structural address of phy_addr "
            <<phy_addr<<", str addr not valid\n";
#endif
        return false;
    }
}

bool OnChipInfo::get_physical_address(uint64_t& phy_addr, uint32_t str_addr, bool update_stats)
{
    curr_timestamp++;

    unsigned int setId = str_addr & indexMask;
    debug_cout << "get physical addr for addr " << (void*)phy_addr << ", set_id: " << setId
        << ", indexMask: " << indexMask << endl;
    std::map<uint32_t, OnChip_SP_Entry*>& sp_map = sp_amc[setId];

    std::map<uint32_t, OnChip_SP_Entry*>::iterator sp_iter =
        sp_map.find(str_addr);
    if (update_stats) {
        ++sp_accesses;
    }
    if (sp_iter == sp_map.end()) {
#ifdef DEBUG
        (*outf)<<"In on-chip get_physical_address of str_addr "
            <<str_addr<<", phy addr not found\n";
#endif
        if (update_stats) {
            ++sp_not_found;
        }
        return false;
    }
    else {
        if (sp_iter->second->valid) {
            phy_addr = sp_iter->second->phy_addr;
            sp_iter->second->last_access = curr_timestamp;
#ifdef DEBUG
            (*outf)<<"In on-chip get_physical_address of str_addr "
                <<str_addr<<", phy addr is "<<phy_addr<<endl;
#endif
            if (update_stats) {
                ++sp_hits;
            }
            return true;
        }
        else {
            if (update_stats) {
                ++sp_invalid;
            }
#ifdef DEBUG
            std::cout <<"In on-chip get_physical_address of str_addr "
                <<str_addr<<", phy addr not valid\n";
#endif

            return false;
        }
    }
}

void OnChipInfo::evict_ps_tlbsync(unsigned int setId)
{
    uint64_t min_timestamp = (uint64_t)(-1);
    uint64_t min_addr = 0;
    uint64_t min_ntr_timestamp = (uint64_t)(-1);
    uint64_t min_ntr_addr = 0;
    std::map<uint64_t, OnChip_PS_Entry*>& ps_map = ps_amc[setId];

    for (std::map<uint64_t, OnChip_PS_Entry*>::iterator ps_iter =
            ps_map.begin();
            ps_iter != ps_map.end(); ps_iter++)
    {
        if (!(ps_iter->second->tlb_resident))
        {
            if (ps_iter->second->last_access < min_ntr_timestamp)
            {
                min_ntr_timestamp = ps_iter->second->last_access;
                min_ntr_addr = ps_iter->first;
            }

            //    delete ps_iter->second;
            //   ps_map.erase(ps_iter);
            //  return;
        }
        if (ps_iter->second->last_access < min_timestamp)
        {
            min_timestamp = ps_iter->second->last_access;
            min_addr = ps_iter->first;
        }
    }

    uint64_t evict_addr = min_addr;
    if (min_ntr_addr != 0)
        evict_addr = min_ntr_addr;

    //Evict LRU line
    std::map<uint64_t, OnChip_PS_Entry*>::iterator ps_iter =
        ps_map.find(evict_addr);
    debug_cout << "[ISBONCHIP] onchip eviction of " << hex << evict_addr 
        << " is tlb_resident: " << ps_iter->second->tlb_resident << endl;
    assert(ps_iter != ps_map.end());
    if (off_chip_writeback && ps_iter->second->dirty) {
        off_chip_info->update(ps_iter->first, ps_iter->second->str_addr);
        #ifdef BLOOM_ISB
          #ifdef BLOOM_ISB_TRAFFIC_DEBUG
          printf("Bloom add a: 0x%lx\n", ps_iter->first);
          #endif
        if (pref->get_bloom_capacity() != 0) {
          pref->add_to_bloom_filter(ps_iter->first);
        }
        #endif
        if (count_off_chip_write_traffic) {
            #ifdef BLOOM_ISB
              #ifdef BLOOM_ISB_TRAFFIC_DEBUG
              printf("Bloom add a: 0x%lx count\n", ps_iter->first);
              #endif
            #endif
            //if (use_write_buffer) {
             //   write_buffer.add(ps_iter->first, ps_iter->second->str_addr);
            //} else {
                access_off_chip(ps_iter->first, ps_iter->second->str_addr, ISB_OCI_REQ_STORE);
            //}
        }
    }
    delete ps_iter->second;
    ps_map.erase(ps_iter);
}

void OnChipInfo::evict_sp_tlbsync(unsigned int setId)
{
    uint64_t min_timestamp = (uint64_t)(-1);
    uint32_t min_addr = 0;
    uint64_t min_ntr_timestamp = (uint64_t)(-1);
    uint32_t min_ntr_addr = 0;
    std::map<uint32_t, OnChip_SP_Entry*>& sp_map = sp_amc[setId];

    for (std::map<uint32_t, OnChip_SP_Entry*>::iterator sp_iter =
            sp_map.begin();
            sp_iter != sp_map.end(); sp_iter++)
    {
        if (!(sp_iter->second->tlb_resident))
        {
            if (sp_iter->second->last_access < min_ntr_timestamp)
            {
                min_ntr_timestamp = sp_iter->second->last_access;
                min_ntr_addr = sp_iter->first;
            }
            //  std::cout << "Not TLB resident: " << std::hex <<
            //      sp_iter->first << std::dec << std::endl;
            //  delete sp_iter->second;
            //  sp_map.erase(sp_iter);
            //  return;
        }
        if (sp_iter->second->last_access < min_timestamp)
        {
            min_timestamp = sp_iter->second->last_access;
            min_addr = sp_iter->first;
        }
    }

    uint32_t evict_addr = min_addr;
    if (min_ntr_addr != 0)
        evict_addr = min_ntr_addr;

    //Evict LRU line
    std::map<uint32_t, OnChip_SP_Entry*>::iterator sp_iter =
        sp_map.find(evict_addr);
    assert(sp_iter != sp_map.end());
    delete sp_iter->second;
    sp_map.erase(sp_iter);

#ifdef COUNT_STREAM_DETAIL
    active_stream_set.erase(evict_addr>>STREAM_MAX_LENGTH_BITS);
#endif
}

void OnChipInfo::evict_ps_lru(unsigned ps_setId)
{
    std::map<uint64_t, OnChip_PS_Entry*>& ps_map = ps_amc[ps_setId];
    uint64_t phy_addr_victim;

    assert(repl_ps.size() > ps_setId);
    debug_cout << "repl_ps " << (void*) repl_ps[ps_setId] << endl;
    phy_addr_victim = repl_ps[ps_setId]->pickVictim();
    std::map<uint64_t, OnChip_PS_Entry*>::iterator ps_iter =
        ps_map.find(phy_addr_victim);
    debug_cout << "[ISBONCHIP] onchip eviction of " << hex << phy_addr_victim  << endl;
    assert(ps_iter != ps_map.end());
    if (off_chip_writeback && ps_iter->second->dirty) {
        off_chip_info->update(ps_iter->first, ps_iter->second->str_addr);
        #ifdef BLOOM_ISB
          #ifdef BLOOM_ISB_TRAFFIC_DEBUG
          printf("Bloom add b: 0x%lx\n", ps_iter->first);
          #endif
        if (pref->get_bloom_capacity() != 0) {
          pref->add_to_bloom_filter(ps_iter->first);
        }
        #endif
        if (count_off_chip_write_traffic) {
            #ifdef BLOOM_ISB
              #ifdef BLOOM_ISB_TRAFFIC_DEBUG
              printf("Bloom add b: 0x%lx count\n", ps_iter->first);
              #endif
            #endif
            //if (use_write_buffer) {
             //   write_buffer.add(ps_iter->first, ps_iter->second->str_addr);
            //} else {
           access_off_chip(ps_iter->first, ps_iter->second->str_addr, ISB_OCI_REQ_STORE);
            //}
        }
    }
    assert(ps_map.count(phy_addr_victim));
    ps_map.erase(phy_addr_victim);
}

void OnChipInfo::evict_sp_lru(unsigned sp_setId)
{
    debug_cout << "evict sp lru for " << sp_setId << endl;
    std::map<uint32_t, OnChip_SP_Entry*>& sp_map = sp_amc[sp_setId];
    assert(sp_map.size() >= amc_assoc);
    uint32_t str_addr_victim;

    assert(repl_sp.size() > sp_setId);
    debug_cout << "repl_sp " << (void*) repl_sp[sp_setId] << endl;
    str_addr_victim = repl_sp[sp_setId]->pickVictim();
    debug_cout << "str victim: " << (void*)(str_addr_victim) << endl;
    for (map<uint32_t, OnChip_SP_Entry*>::iterator it = sp_map.begin();
            it != sp_map.end(); ++it)
        debug_cout << "sp_map[" << it->first << "] = " << it->second
            << endl;
    assert(sp_map.count(str_addr_victim));
    sp_map.erase(str_addr_victim);
#ifdef COUNT_STREAM_DETAIL
    active_stream_set.erase(str_addr_victim>>STREAM_MAX_LENGTH_BITS);
#endif
}

void OnChipInfo::evict_ps(unsigned ps_setId)
{
    std::map<uint64_t, OnChip_PS_Entry*>& ps_map = ps_amc[ps_setId];
    debug_cout << "Trying to evict for ps_setId: " << ps_setId
        << ", size: " << ps_map.size() << endl;

    switch(repl_policy) {
        case ISB_REPL_TYPE_LRU:
        case ISB_REPL_TYPE_LFU:
        case ISB_REPL_TYPE_METAPREF:
        case ISB_REPL_TYPE_BULKLRU:
        case ISB_REPL_TYPE_BULKMETAPREF:
            while (ps_map.size() >= amc_assoc) {
                evict_ps_lru(ps_setId);
            }
            break;
        case ISB_REPL_TYPE_TLBSYNC:
        case ISB_REPL_TYPE_TLBSYNC_METAPREF:
        case ISB_REPL_TYPE_TLBSYNC_BULKMETAPREF:
            while (ps_map.size() > amc_assoc) {
                evict_ps_tlbsync(ps_setId);
            }
            break;
        case ISB_REPL_TYPE_PERFECT:
            // DO NOTHING
            break;
        default:
            // Impossible to reach here
            cerr << "Unknown Repl Policy"  << endl;
    }
}

void OnChipInfo::evict_sp(unsigned sp_setId)
{
    std::map<uint32_t, OnChip_SP_Entry*>& sp_map = sp_amc[sp_setId];
    debug_cout << "Trying to evict for sp_setId: " << sp_setId
        << ", size: " << sp_map.size() << endl;

    switch(repl_policy) {
        case ISB_REPL_TYPE_LRU:
        case ISB_REPL_TYPE_LFU:
        case ISB_REPL_TYPE_METAPREF:
        case ISB_REPL_TYPE_BULKLRU:
        case ISB_REPL_TYPE_BULKMETAPREF:
            while (sp_map.size() >= amc_assoc) {
                evict_sp_lru(sp_setId);
            }
            break;
        case ISB_REPL_TYPE_TLBSYNC:
        case ISB_REPL_TYPE_TLBSYNC_METAPREF:
        case ISB_REPL_TYPE_TLBSYNC_BULKMETAPREF:
            while (sp_map.size() >= amc_assoc) {
                evict_sp_tlbsync(sp_setId);
            }
            break;
        case ISB_REPL_TYPE_PERFECT:
            // DO NOTHING
            break;
        default:
            // Impossible to reach here
            cerr << "Unknown Repl Policy" << endl;
    }
}

void OnChipInfo::update(uint64_t phy_addr, uint32_t str_addr, bool set_dirty)
{
#ifdef DEBUG
    (*outf)<<"In on_chip_info update, phy_addr is "
        <<phy_addr<<", str_addr is "<<str_addr<<endl;
#endif

    unsigned int ps_setId = (phy_addr >> 6) & indexMask;
    std::map<uint64_t, OnChip_PS_Entry*>& ps_map = ps_amc[ps_setId];

    unsigned int sp_setId = str_addr & indexMask;
    std::map<uint32_t, OnChip_SP_Entry*>& sp_map = sp_amc[sp_setId];
    ++update_count;

    debug_cout << "[ISBONCHIP] onchip update: phy_addr=" << (void*)phy_addr
        << " str_addr=" << (void*)str_addr
        << " ps_setId=" << ps_setId
        << " sp_setId=" << sp_setId
        << endl;

#if 0
    evict(ps_setId, sp_setId);
#endif

    //PS Map Update
    std::map<uint64_t, OnChip_PS_Entry*>::iterator ps_iter =
        ps_map.find(phy_addr);
    if (ps_iter == ps_map.end()) {
        evict_ps(ps_setId);
        OnChip_PS_Entry* ps_entry = new OnChip_PS_Entry();
        ps_map[phy_addr] = ps_entry;
        ps_map[phy_addr]->set(str_addr);
        ps_map[phy_addr]->last_access = curr_timestamp;
        if (set_dirty) {
            ps_map[phy_addr]->dirty = true;
        }
    }
    else if (ps_iter->second->str_addr != str_addr) {
#ifdef OVERWRITE_OFF_CHIP
        ps_iter->second->set(str_addr);
        ps_iter->second->last_access = curr_timestamp;
        if (set_dirty) {
            ps_map[phy_addr]->dirty = true;
        } else {
            ps_map[phy_addr]->dirty = false;
        }
#else
        if (set_dirty) {
            ps_iter->second->set(str_addr);
            ps_iter->second->last_access = curr_timestamp;
            ps_map[phy_addr]->dirty = true;
        }
#endif
    } else {
        ps_iter->second->last_access = curr_timestamp;
        if (!set_dirty) {
            ps_map[phy_addr]->dirty = false;
        }
    }

    //SP Map Update
    std::map<uint32_t, OnChip_SP_Entry*>::iterator sp_iter =
        sp_map.find(str_addr);
    if (sp_iter == sp_map.end()) {
        evict_sp(sp_setId);
        OnChip_SP_Entry* sp_entry = new OnChip_SP_Entry();
        sp_map[str_addr] = sp_entry;
        sp_map[str_addr]->set(phy_addr);
        sp_map[str_addr]->last_access = curr_timestamp;
    }
    else {
#ifdef OVERWRITE_OFF_CHIP
        sp_iter->second->set(phy_addr);
        sp_iter->second->last_access = curr_timestamp;
#else
        if(set_dirty) {
            sp_iter->second->set(phy_addr);
            sp_iter->second->last_access = curr_timestamp;
        }
#endif
    }

    if (repl_policy == ISB_REPL_TYPE_LRU || repl_policy == ISB_REPL_TYPE_LFU
            || repl_policy == ISB_REPL_TYPE_METAPREF
            || repl_policy == ISB_REPL_TYPE_BULKLRU
            || repl_policy == ISB_REPL_TYPE_BULKMETAPREF) {
        // LRU or LFU Replacement
        repl_ps[ps_setId]->addEntry(phy_addr);
        repl_sp[sp_setId]->addEntry(str_addr);
    }
    debug_cout << "ps_set_size: " << ps_map.size()
        << ", sp_set_size: " << sp_map.size()
        << endl;
}

void OnChipInfo::invalidate(uint64_t phy_addr, uint32_t str_addr)
{
    unsigned int ps_setId = (phy_addr >> 6) & indexMask;
    std::map<uint64_t, OnChip_PS_Entry*>& ps_map = ps_amc[ps_setId];

    unsigned int sp_setId = str_addr & indexMask;
    std::map<uint32_t, OnChip_SP_Entry*>& sp_map = sp_amc[sp_setId];
#ifdef DEBUG
    (*outf)<<"In on_chip_info invalidate, phy_addr is "
        <<phy_addr<<", str_addr is "<<str_addr<<endl;
#endif
    //PS Map Invalidate
    std::map<uint64_t, OnChip_PS_Entry*>::iterator ps_iter =
        ps_map.find(phy_addr);
    if (ps_iter != ps_map.end()) {
        ps_iter->second->reset();
        delete ps_iter->second;
        ps_map.erase(ps_iter);
        if (repl_policy == ISB_REPL_TYPE_LRU || repl_policy == ISB_REPL_TYPE_LFU
                || repl_policy == ISB_REPL_TYPE_METAPREF
                || repl_policy == ISB_REPL_TYPE_BULKLRU
                || repl_policy == ISB_REPL_TYPE_BULKMETAPREF) {
            repl_ps[ps_setId]->eraseEntry(phy_addr);
        }
    }

    //SP Map Invalidate
    std::map<uint32_t, OnChip_SP_Entry*>::iterator sp_iter =
        sp_map.find(str_addr);
    if (sp_iter != sp_map.end()) {
        sp_iter->second->reset();
        //delete sp_iter->second;
        //sp_map.erase(sp_iter);
    }
    else {
        //TODO TBD
    }

#ifdef DEBUG
    (*outf)<<"In on_chip_info invalidate prefetch buffer, phy_addr is "
        <<phy_addr<<", str_addr is "<<str_addr<<endl;
#endif
}

void OnChipInfo::increase_confidence(uint64_t phy_addr)
{
    unsigned int ps_setId = (phy_addr >> 6) & indexMask;
    std::map<uint64_t, OnChip_PS_Entry*>& ps_map = ps_amc[ps_setId];
#ifdef DEBUG
    (*outf)<<"In on_chip_info increase_confidence, phy_addr is "
        <<phy_addr<<endl;
#endif
    std::map<uint64_t, OnChip_PS_Entry*>::iterator ps_iter =
        ps_map.find(phy_addr);
    if (ps_iter != ps_map.end()) {
        ps_iter->second->increase_confidence();
    }
}

bool OnChipInfo::lower_confidence(uint64_t phy_addr)
{
    bool ret = false;

    unsigned int ps_setId = (phy_addr >> 6) & indexMask;
    std::map<uint64_t, OnChip_PS_Entry*>& ps_map = ps_amc[ps_setId];
#ifdef DEBUG
    (*outf)<<"In on_chip_info lower_confidence, phy_addr is "
        <<phy_addr<<endl;
#endif

    std::map<uint64_t, OnChip_PS_Entry*>::iterator ps_iter =
        ps_map.find(phy_addr);
    if (ps_iter != ps_map.end()) {
        ret = ps_iter->second->lower_confidence();
    }
    return ret;
}

void OnChipInfo::mark_not_tlb_resident(uint64_t phy_addr)
{
    unsigned int ps_setId = (phy_addr >> 6) & indexMask;
    std::map<uint64_t, OnChip_PS_Entry*>& ps_map = ps_amc[ps_setId];

    std::map<uint64_t, OnChip_PS_Entry*>::iterator ps_iter =
        ps_map.find(phy_addr);
    if (ps_iter != ps_map.end() )
    {
        uint32_t str_addr = ps_iter->second->str_addr;

        unsigned int sp_setId = str_addr & indexMask;
        std::map<uint32_t, OnChip_SP_Entry*>& sp_map = sp_amc[sp_setId];
        std::map<uint32_t, OnChip_SP_Entry*>::iterator sp_iter =
            sp_map.find(str_addr);
        if (sp_iter != sp_map.end())
            sp_iter->second->mark_tlb_evicted();
        ps_iter->second->mark_tlb_evicted();
    }
}

void OnChipInfo::mark_tlb_resident(uint64_t phy_addr)
{
    unsigned int ps_setId = (phy_addr >> 6) & indexMask;
    std::map<uint64_t, OnChip_PS_Entry*>& ps_map = ps_amc[ps_setId];

    std::map<uint64_t, OnChip_PS_Entry*>::iterator ps_iter =
        ps_map.find(phy_addr);
    if (ps_iter != ps_map.end() )
    {
        uint32_t str_addr = ps_iter->second->str_addr;

        unsigned int sp_setId = str_addr & indexMask;
        std::map<uint32_t, OnChip_SP_Entry*>& sp_map = sp_amc[sp_setId];
        std::map<uint32_t, OnChip_SP_Entry*>::iterator sp_iter =
            sp_map.find(str_addr);
        //assert(sp_iter != sp_map.end());
        if (sp_iter != sp_map.end())
            sp_iter->second->mark_tlb_resident();
        ps_iter->second->mark_tlb_resident();
    }

}

#ifdef COUNT_STREAM_DETAIL
bool OnChipInfo::is_trigger_access(uint32_t str_addr)
{
    if (!active_stream_set.count(str_addr >> STREAM_MAX_LENGTH_BITS)) {
        active_stream_set.insert(str_addr >> STREAM_MAX_LENGTH_BITS);
        return true;
    }

    return false;
}
#endif

void OnChipInfo::fetch_bulk(uint64_t phy_addr, off_chip_req_type_t req_type)
{
    assert(0);
    assert(req_type != ISB_OCI_REQ_STORE);
    uint64_t region_addr = phy_addr >> (log_cacheblocksize+log_regionsize);
    debug_cout << "Fetch Bulk for phy_addr: " << phy_addr << ", region_addr: " << region_addr <<endl;

    for (unsigned region_offset = 0; region_offset < regionsize; region_offset+=16) {
        uint64_t phy_addr_to_fetch = ((region_addr << log_regionsize) +
                region_offset) << log_cacheblocksize;
        debug_cout << "In region " << region_addr << " offset " << region_offset
            << " we fetch " << phy_addr_to_fetch;
        uint32_t str_addr_to_fetch;
        bool on_chip_str_addr_exists = get_structural_address(phy_addr_to_fetch, str_addr_to_fetch, false);
        if (!on_chip_str_addr_exists) {
            bool off_chip_str_addr_exists = off_chip_info->get_structural_address(phy_addr_to_fetch, str_addr_to_fetch);
            if (!off_chip_str_addr_exists) {
                str_addr_to_fetch = INVALID_ADDR;
            }
            debug_cout << "not on chip!" << str_addr_to_fetch << endl;
            // Don't trigger metapref on metapref hit!
            if (ideal_off_chip_transaction) {
                ++bulk_actual;
                update(phy_addr_to_fetch, str_addr_to_fetch, false);
            } else {
                ++bulk_actual;
                debug_cout << hex << "Inside fetch bulk " << phy_addr_to_fetch
                    << " to " << str_addr_to_fetch << endl;
                access_off_chip(phy_addr_to_fetch, str_addr_to_fetch, req_type);
            }
        } else {
            debug_cout << "on chip! " << str_addr_to_fetch << endl;
        }
    }
}

void OnChipInfo::doPrefetch(uint64_t phy_addr, uint32_t str_addr, bool is_second)
{
    //cout << "Prefetch " << hex << str_addr << dec << " " << is_second << endl;
    std::vector<uint64_t> pref_list =
        oci_pref.getNextAddress(0, phy_addr, str_addr);
    uint64_t pref_phy_addr;
    uint32_t pref_str_addr = str_addr;
    uint64_t exist_phy_addr;
    for (unsigned i = 0; i < pref_list.size(); ++i) {
        pref_phy_addr = pref_list[i];
        ++pref_str_addr;

        debug_cout << hex << "ONCHIPINFOPREF STR ADDR: " << str_addr
            << ", PHY ADDR: " << phy_addr
            << ", PREF STR ADDR: " << pref_str_addr
            << ", PREF PHY ADDR: " << pref_phy_addr
            << endl;
        bool phy_on_chip_exist = get_physical_address(exist_phy_addr, pref_str_addr, false);

        if (phy_on_chip_exist) {
            if (exist_phy_addr == pref_phy_addr) {
                ++metapref_duplicate;
            } else {
                ++metapref_conflict;
            }
        } else {
            ++metapref_actual;
        }
        if (!phy_on_chip_exist) {
            if (ideal_off_chip_transaction) {
                if (pref_phy_addr != INVALID_ADDR) {
                    update(pref_phy_addr, pref_str_addr, false);
                }
            } else {
                debug_cout << "Metapref " << (void*)pref_phy_addr << " to " << (void*)pref_str_addr << endl;
                if (is_second) {
                    access_off_chip(pref_phy_addr, pref_str_addr, ISB_OCI_REQ_LOAD_SP2);
                } else {
                    access_off_chip(pref_phy_addr, pref_str_addr, ISB_OCI_REQ_LOAD_SP1);
                }
            }
        }
        ++metapref_count;
    }
}

void OnChipInfo::doPrefetchBulk(uint64_t phy_addr, uint32_t str_addr, bool is_second)
{
    assert(0);
    std::vector<uint64_t> pref_list =
        oci_pref.getNextAddress(0, phy_addr, str_addr);
    uint64_t pref_phy_addr;
    uint32_t pref_str_addr = str_addr;
    for (unsigned i = 0; i < pref_list.size(); ++i) {
        pref_phy_addr = pref_list[i];
        debug_cout << hex << "STR ADDR: " << str_addr
            << ", PHY ADDR: " << phy_addr
            << ", PREF PHY ADDR: " << pref_phy_addr
            << endl;
        ++metapref_count;
        uint64_t exist_phy_addr;
        bool phy_on_chip_exist = get_physical_address(exist_phy_addr, pref_str_addr, false);
        ++pref_str_addr;
        if (phy_on_chip_exist) {
            if (exist_phy_addr == pref_phy_addr) {
                ++metapref_duplicate;
            } else {
                ++metapref_conflict;
            }
        } else {
            ++metapref_actual;
        }

        fetch_bulk(pref_phy_addr, is_second?ISB_OCI_REQ_LOAD_SP2:ISB_OCI_REQ_LOAD_SP1);
    }
}

void OnChipInfo::print()
{
    std::cerr << "OnChipInfo PHY ACCESS: " << ps_accesses
        << ", HITS: " << ps_hits
        << ", PREFHIT: " << ps_prefetch_hits << std::endl;
    std::cerr << "OnChipInfo STR ACCESS: " << sp_accesses
        << ", HITS: " << sp_hits
        << ", PREFHIT: " << sp_prefetch_hits << std::endl;
}

void OnChipInfo::write_off_chip_region(uint64_t phy_addr, uint32_t str_addr, off_chip_req_type_t req_type)
{
    // Only Support store request for write_off_chip_region
    assert(req_type == ISB_OCI_REQ_STORE);
    bool success;
    phy_addr = (phy_addr>>10)<<10;
    for (unsigned offset = 0; offset < 16; ++offset) {
        success = get_structural_address(phy_addr + (offset<<6), str_addr, false, true);
        if (success) {
            debug_cout << "OCIFILLER UPDATE PHY REGION: " << (void*)(phy_addr+(offset<<6)) << " TO "
                << (void*)str_addr << endl;
            off_chip_info->update(phy_addr+(offset<<6), str_addr);
            #ifdef BLOOM_ISB
              #ifdef BLOOM_ISB_TRAFFIC_DEBUG
              printf("Bloom add i: 0x%lx\n", phy_addr+(offset<<6));
              #endif
            if (pref->get_bloom_capacity() != 0) {
                pref->add_to_bloom_filter(phy_addr+(offset<<6));
            }
            #endif
        }
    }
}

void OnChipInfo::update_phy_region(uint64_t phy_addr)
{
    bool success = true;
    phy_addr = (phy_addr>>10)<<10;
    uint32_t str_addr;

    for (unsigned offset = 0; offset < 16; ++ offset) {
        success = off_chip_info->get_structural_address(phy_addr + (offset<<6), str_addr);
        if (success) {
            debug_cout << "UPDATE PHY REGION: " << (void*)(phy_addr+(offset<<6)) << " TO "
                << (void*)str_addr << endl;
            update(phy_addr+(offset<<6), str_addr, false);
        }
    }
    pref->issue_prefetch_buffer();
}

void OnChipInfo::update_str_region(uint32_t str_addr)
{
    bool success;
    str_addr = (str_addr>>3)<<3;
    uint64_t phy_addr;
    for (unsigned offset = 0; offset < 8; ++ offset) {
        success = off_chip_info->get_physical_address(phy_addr, str_addr+offset);
        if (success) {
            //cout << "                  " << hex << (str_addr+offset) << dec << endl;
            debug_cout << "OCIFILLER UPDATE STR REGION: " << (void*)(phy_addr+(offset<<6)) << " TO "
                << (void*)str_addr << endl;
            debug_cout << "OCIFILLER UPDATE STR REGION: " << (void*)(phy_addr) << " TO "
                << (void*)(str_addr+offset) << endl;
            update(phy_addr, str_addr+offset, false);
        }
    }
    pref->issue_prefetch_buffer();
}


void OnChipInfo::access_off_chip(uint64_t phy_addr, uint32_t str_addr, off_chip_req_type_t req_type)
{
    //insert_metadata(phy_addr, str_addr, req_type);

    debug_cout << "Inside access_off_chip: phy_addr: " << (void*)phy_addr
        << ", str_addr: " << (void*)str_addr
        << ", req_type: " << req_type
        << endl;

   if (req_type == ISB_OCI_REQ_STORE) {
        pref->write_metadata(phy_addr>>10, req_type);
        assert(phy_addr != INVALID_ADDR && str_addr != INVALID_ADDR);
        if (repl_policy == ISB_REPL_TYPE_LRU || repl_policy == ISB_REPL_TYPE_LFU
                || repl_policy == ISB_REPL_TYPE_METAPREF) {
            off_chip_info->update(phy_addr, str_addr);
                        #ifdef BLOOM_ISB
                          #ifdef BLOOM_ISB_TRAFFIC_DEBUG
                          printf("Bloom add j: 0x%lx\n", phy_addr);
                          printf("Bloom add j: 0x%lx count\n", phy_addr);
                          #endif
                        if (pref->get_bloom_capacity() != 0) {
                           pref->add_to_bloom_filter(phy_addr);
                        }
                        #endif
        } else if (repl_policy == ISB_REPL_TYPE_BULKLRU
                || repl_policy == ISB_REPL_TYPE_BULKMETAPREF) {
            write_off_chip_region(phy_addr, str_addr, req_type);
        }
    } else if (req_type == ISB_OCI_REQ_LOAD_PS) {
        assert(phy_addr != INVALID_ADDR);
        if (repl_policy == ISB_REPL_TYPE_LRU || repl_policy == ISB_REPL_TYPE_LFU
                || repl_policy == ISB_REPL_TYPE_METAPREF) {


    #ifdef BLOOM_ISB
    // Check bloom filter
    if (pref->get_bloom_capacity() != 0) {
      if (!pref->lookup_bloom_filter(phy_addr>>10)) {
        return;
      }
    }
    #endif

            pref->read_metadata((uint64_t)(phy_addr>>10), phy_addr, str_addr, req_type);
            //off_chip_info->get_structural_address(phy_addr, str_addr);
            //if (str_addr != INVALID_ADDR) {
             //   update(phy_addr, str_addr, false);
            //}
        } else if (repl_policy == ISB_REPL_TYPE_BULKLRU
                || repl_policy == ISB_REPL_TYPE_BULKMETAPREF) {
            //bool ret = update_phy_region(phy_addr); //Done: Make sure not already on chip
#ifdef BLOOM_ISB
    if (pref->get_bloom_capacity() != 0) {
        if (!pref->lookup_bloom_filter(phy_addr)) {
            //cout << "Filter PS read to " << hex << phy_addr << dec << endl;
            return;
        }
        //else
         //   cout << "Allow PS read to " << hex << phy_addr << dec << endl;
    }
#endif

            //assert(ret);
            pref->read_metadata((uint64_t)(phy_addr>>10), phy_addr, str_addr, req_type);
        }
        // TODO: This should actually be done after the data has arrived!
        //debug_cout << "str_addr: " << str_addr << ", invalid_addr: " << INVALID_ADDR
         //   << ", equal = " << (str_addr == INVALID_ADDR) << endl;
        //if (str_addr != INVALID_ADDR) {
         //   pref->isb_predict(phy_addr, str_addr);
        //}
    } else if (req_type == ISB_OCI_REQ_LOAD_SP1 || req_type == ISB_OCI_REQ_LOAD_SP2) {
        assert(str_addr != INVALID_ADDR);
        if (repl_policy == ISB_REPL_TYPE_LRU || repl_policy == ISB_REPL_TYPE_LFU
                || repl_policy == ISB_REPL_TYPE_METAPREF) {
            pref->read_metadata((uint64_t)(str_addr>>3), phy_addr, str_addr, req_type);
          //  off_chip_info->get_physical_address(phy_addr, str_addr);
            //if (phy_addr != INVALID_ADDR) {
              //  update(phy_addr, str_addr, false);
     //       }
        } else if (repl_policy == ISB_REPL_TYPE_BULKLRU
                || repl_policy == ISB_REPL_TYPE_BULKMETAPREF) {
        //    update_str_region(str_addr);
            pref->read_metadata((uint64_t)(str_addr>>3), phy_addr, str_addr, req_type);
        }
    }
}

void OnChipInfo::insert_metadata_ps(uint64_t phy_addr)
{
    debug_cout << "Inside insert_metdata_ps: phy_addr: " << (void*)phy_addr << endl;

    uint32_t str_addr;
    assert(phy_addr != INVALID_ADDR);
    if (repl_policy == ISB_REPL_TYPE_LRU || repl_policy == ISB_REPL_TYPE_LFU
            || repl_policy == ISB_REPL_TYPE_METAPREF) {

        bool ret = off_chip_info->get_structural_address(phy_addr, str_addr);
        if (ret && str_addr != INVALID_ADDR) {
            update(phy_addr, str_addr, false);
        }
    } else if (repl_policy == ISB_REPL_TYPE_BULKLRU
            || repl_policy == ISB_REPL_TYPE_BULKMETAPREF) {
        update_phy_region(phy_addr);
    }

    //bool ret = off_chip_info->get_structural_address(phy_addr, str_addr);
    //if (ret && (str_addr != INVALID_ADDR)) {
        //cout << "Doing delayed prediction on " << hex << phy_addr << " " << str_addr << dec << endl;
     //   pref->isb_predict(phy_addr, str_addr);
    //}
}

void OnChipInfo::insert_metadata_sp(uint32_t str_addr)
{
    debug_cout << "Inside insert_metdata: phy_addr: "
        << ", str_addr: " << (void*)str_addr
        << endl;
    debug_cout << "Completed metadata " << hex << str_addr << dec << endl;
    assert(str_addr != INVALID_ADDR);
    uint64_t phy_addr;
    if (repl_policy == ISB_REPL_TYPE_LRU || repl_policy == ISB_REPL_TYPE_LFU
            || repl_policy == ISB_REPL_TYPE_METAPREF) {
        bool ret = off_chip_info->get_physical_address(phy_addr, str_addr);
        if (ret && phy_addr != INVALID_ADDR) {
            update(phy_addr, str_addr, false);
        }
    } else if (repl_policy == ISB_REPL_TYPE_BULKLRU
            || repl_policy == ISB_REPL_TYPE_BULKMETAPREF) {
        update_str_region(str_addr);
    }

}



void ISBOnChipPref::init(OffChipInfo* off_chip_info, int metapref_degree)
{
    this->off_chip_info = off_chip_info;
    degree = metapref_degree;

    metapref_success = 0, metapref_not_found = 0, metapref_stream_end = 0;
}

vector<uint64_t> ISBOnChipPref::getNextAddress(uint64_t pc, uint64_t paddr, uint32_t saddr)
{
    // This approach uses Structure uint64_tess to Prefetch
    // We might use other features if appropriate
    //
    vector<uint64_t> result_list;
    debug_cout << hex << "isb on chip pref receive: pc: " << pc << ", paddr: " << paddr << ", saddr: " << saddr
        << ", degree = " << degree << endl;

    uint32_t pref_saddr;
    uint64_t pref_paddr;
    for (unsigned i = 1; i <= degree; ++i) {
        pref_saddr = saddr + i;
        if (pref_saddr % STREAM_MAX_LENGTH == 0) {
            ++metapref_stream_end;
            break;
        }

        bool success =
            off_chip_info->get_physical_address(pref_paddr, pref_saddr);

        off_chip_info->phy_access_count++;
        if (success) {
            off_chip_info->phy_success_count++;
            metapref_success++;
        } else {
            metapref_not_found++;
        }

#ifdef DEBUG
        std::cerr << " ISB metadata Prefetch success: " << success
            << " PHY ADDR: " << pref_paddr
            << " STR ADDR: " << pref_saddr
            << std::endl;
#endif
        if (!success) {
            pref_paddr = INVALID_ADDR;
        }

        result_list.push_back(pref_paddr);
    }

    return result_list;
}

void OnChipReplacementLRU::addEntry(uint64_t entry)
{
    map<uint64_t, unsigned>::iterator it = entry_list.find(entry);
    if (it != entry_list.end()) {
        it->second = 0;
    } else {
        for (map<uint64_t, unsigned>::iterator it = entry_list.begin();
                it != entry_list.end(); ++it) {
            ++it->second;
        }
        entry_list[entry] = 0;
    }
}

void OnChipReplacementLRU::eraseEntry(uint64_t entry)
{
    entry_list.erase(entry);
}

uint64_t OnChipReplacementLRU::pickVictim()
{
    assert(entry_list.size() != 0);
    unsigned max_ts = 0;
    map<uint64_t, unsigned>::iterator max_it = entry_list.begin();
    for (map<uint64_t, unsigned>::iterator it = entry_list.begin();
            it != entry_list.end(); ++it) {
        if (it->second > max_ts) {
            max_ts = it->second;
            max_it = it;
        }
    }

    assert(max_it != entry_list.end());
    uint64_t addr = max_it->first;
    entry_list.erase(max_it);

    return addr;
}

void OnChipReplacementLFU::addEntry(uint64_t entry)
{
    map<uint64_t, unsigned>::iterator it = entry_list.find(entry);
    if (it != entry_list.end()) {
        ++it->second;
    } else {
        entry_list[entry] = 1;
    }
}

void OnChipReplacementLFU::eraseEntry(uint64_t entry)
{
    entry_list.erase(entry);
}

uint64_t OnChipReplacementLFU::pickVictim()
{
    unsigned min_ts = UINT32_MAX;
    map<uint64_t, unsigned>::iterator min_it = entry_list.begin();
    for (map<uint64_t, unsigned>::iterator it = entry_list.begin();
            it != entry_list.end(); ++it) {
        if (it->second < min_ts) {
            min_ts = it->second;
            min_it = it;
        }
    }

    uint64_t addr = min_it->first;
    entry_list.erase(min_it);

    return addr;
}

OnChipBandwidthConstraint::OnChipBandwidthConstraint()
{
    // XXX Magic number
    tick_interval = 2;
    next_available_tick = 0;
}

bool OnChipBandwidthConstraint::allow_next_access(uint64_t current_tick)
{
    if (current_tick >= next_available_tick) {
        return true;
    } else {
        return false;
    }
}

void OnChipBandwidthConstraint::make_access(uint64_t current_tick)
{
    next_available_tick = current_tick + tick_interval;
}



