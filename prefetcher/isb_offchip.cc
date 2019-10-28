
#include <iostream>
#include <map>

#include "isb_offchip.h"

using namespace std;

//#define DEBUG

#ifdef DEBUG
#define debug_cout std::cerr << "[ISBOFFCHIP] "
#else
#define debug_cout if (0) std::cerr
#endif

OffChipInfo::OffChipInfo()
{
    reset();
    phy_access_count = 0;
    str_access_count = 0;
    phy_success_count = 0;
    str_success_count = 0;
    update_count = 0;
}

void OffChipInfo::reset()
{
    ps_map.clear();
    sp_map.clear();
}

size_t OffChipInfo::get_ps_size()
{
    return ps_map.size();
}

size_t OffChipInfo::get_sp_size()
{
    return sp_map.size();
}

bool OffChipInfo::exists_off_chip(Addr phy_addr)
{
    map<Addr, OffChip_PS_Entry*>::iterator ps_iter = ps_map.find(phy_addr);
    if (ps_iter == ps_map.end())
        return false;
    else
    {
        if ( ps_iter->second->valid )
            return true;
        else
            return false;
    }
}

bool OffChipInfo::get_structural_address(Addr phy_addr, StrAddr& str_addr)
{
    //cerr<<"here "<<phy_addr<<endl;
    map<Addr, OffChip_PS_Entry*>::iterator ps_iter = ps_map.find(phy_addr);
    if (ps_iter == ps_map.end()) {
        #ifdef DEBUG
          (*outf) << "In off-chip get_structural address of phy_addr "
              << phy_addr << ", str addr not found\n";
        #endif
        return false;
    }
    else {
        //if (ps_iter->second->valid && ps_iter->second->cached) {
        if (ps_iter->second->valid) {
            assert(ps_iter->second->cached);
                str_addr = ps_iter->second->str_addr;
                #ifdef DEBUG
                        (*outf)<<"In off-chip get_structural address of phy_addr "
                            <<phy_addr<<", str addr is "<<str_addr<<endl;
                #endif
                return true;
        }
        else {
                #ifdef DEBUG
                        (*outf)<<"In off-chip get_structural address of phy_addr "
                            <<phy_addr<<", str addr not valid\n";
                #endif
                return false;
        }
    }
}

bool OffChipInfo::get_physical_address(Addr& phy_addr, StrAddr str_addr)
{
    map<StrAddr, OffChip_SP_Entry*>::iterator sp_iter = sp_map.find(str_addr);
    if (sp_iter == sp_map.end()) {
        #ifdef DEBUG
          (*outf)<<"In off-chip get_physical_address of str_addr "
              <<str_addr<<", phy addr not found\n";
        #endif
        return false;
    }
    else {
        //if (sp_iter->second->valid && sp_iter->second->cached) {
        if (sp_iter->second->valid) {
            assert(sp_iter->second->cached);
                phy_addr = sp_iter->second->phy_addr;
                #ifdef DEBUG
                  (*outf)<<"In off-chip get_physical_address of str_addr "
                      <<str_addr<<", phy addr is "<<phy_addr<<endl;
                #endif
                return true;
        }
        else {
                #ifdef DEBUG
                  (*outf)<<"In off-chip get_physical_address of str_addr "
                      <<str_addr<<", phy addr not valid\n";
                #endif

                return false;
        }
    }
}

void OffChipInfo::update(Addr phy_addr, StrAddr str_addr)
{
    debug_cout<<"In off_chip_info update, phy_addr is "
             <<hex<<phy_addr<<", str_addr is "<<str_addr<<endl;

    //PS Map Update
    map<Addr, OffChip_PS_Entry*>::iterator ps_iter = ps_map.find(phy_addr);
    if (ps_iter == ps_map.end()) {
        OffChip_PS_Entry* ps_entry = new OffChip_PS_Entry();
        ps_map[phy_addr] = ps_entry;
        ps_map[phy_addr]->set(str_addr);
    }
    else {
        ps_iter->second->set(str_addr);
    }

    //SP Map Update
    map<StrAddr, OffChip_SP_Entry*>::iterator sp_iter = sp_map.find(str_addr);
    if (sp_iter == sp_map.end()) {
        OffChip_SP_Entry* sp_entry = new OffChip_SP_Entry();
        sp_map[str_addr] = sp_entry;
        sp_map[str_addr]->set(phy_addr);
    }
    else {
        sp_iter->second->set(phy_addr);
    }
}

void OffChipInfo::invalidate(Addr phy_addr, StrAddr str_addr)
{
    #ifdef DEBUG
         (*outf)<<"In off_chip_info invalidate, phy_addr is "
             <<phy_addr<<", str_addr is "<<str_addr<<endl;
    #endif
    //PS Map Invalidate
    map<Addr, OffChip_PS_Entry*>::iterator ps_iter = ps_map.find(phy_addr);
    if (ps_iter != ps_map.end()) {
        ps_iter->second->reset();
        //delete ps_iter->second;
        //ps_map.erase(ps_iter);
    }
    else {
        //TODO TBD
    }

    //SP Map Invalidate
    map<StrAddr, OffChip_SP_Entry*>::iterator sp_iter = sp_map.find(str_addr);
    if (sp_iter != sp_map.end()) {
        sp_iter->second->reset();
        delete sp_iter->second;
        sp_map.erase(sp_iter);
    }
    else {
        //TODO TBD
    }
}

void OffChipInfo::increase_confidence(Addr phy_addr)
{
    #ifdef DEBUG
         (*outf)<<"In off_chip_info increase_confidence, phy_addr is "
             <<phy_addr<<endl;
    #endif
    map<Addr, OffChip_PS_Entry*>::iterator ps_iter = ps_map.find(phy_addr);
    if (ps_iter != ps_map.end()) {
        ps_iter->second->increase_confidence();
    }
    else {
        assert(0);
    }
}

bool OffChipInfo::lower_confidence(Addr phy_addr)
{
    bool ret = false;

    #ifdef DEBUG
         (*outf)<<"In off_chip_info lower_confidence, phy_addr is "
             <<phy_addr<<endl;
    #endif

    map<Addr, OffChip_PS_Entry*>::iterator ps_iter = ps_map.find(phy_addr);
    if (ps_iter != ps_map.end()) {
        ret = ps_iter->second->lower_confidence();
    }
    else {
        assert(0);
    }
    return ret;
}

void OffChipInfo::mark_cached(Addr phy_addr)
{
    map<Addr, OffChip_PS_Entry*>::iterator ps_iter = ps_map.find(phy_addr);
    if (ps_iter != ps_map.end() && ps_iter->second->valid)
    {
        ps_iter->second->mark_cached();
        StrAddr str_addr = ps_iter->second->str_addr;
        map<StrAddr, OffChip_SP_Entry*>::iterator sp_iter = sp_map.find(str_addr);
        assert(sp_iter != sp_map.end());
        sp_iter->second->mark_cached();
    }
}

void OffChipInfo::mark_evicted(Addr phy_addr)
{
    map<Addr, OffChip_PS_Entry*>::iterator ps_iter = ps_map.find(phy_addr);
    if (ps_iter != ps_map.end() && ps_iter->second->valid)
    {
        ps_iter->second->mark_evicted();
        StrAddr str_addr = ps_iter->second->str_addr;
        map<StrAddr, OffChip_SP_Entry*>::iterator sp_iter = sp_map.find(str_addr);
        assert(sp_iter != sp_map.end());
        sp_iter->second->mark_evicted();
    }
}

void OffChipInfo::print()
{
    for (map<StrAddr, OffChip_SP_Entry*>::iterator it = sp_map.begin();
            it != sp_map.end(); it++)
    {
        if (it->second->valid)
            cout << hex << it->first << "  "
                << (it->second)->phy_addr << endl;
    }
}

