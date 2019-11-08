#include "reeses_offchip.h"

using namespace std;

namespace reeses {

bool OffChipInfo::get_structural_address(address addr, uint32_t& str_addr) {
    if (ps_map.find(addr) == ps_map.end()) {
        return false;
    } else if (ps_map[addr].valid) {
        str_addr = ps_map[addr].str_addr;
        return true;
    } else {
        return false;
    }
}

bool OffChipInfo::get_physical_data(TUEntry* &data, uint32_t str_addr) {
    if (sp_map.find(str_addr) == sp_map.end()) {
        return false;
    } else if (sp_map[str_addr].valid) {
        data = sp_map[str_addr].data;
        return true;
    } else {
        return false;
    }
}

void OffChipInfo::update(TUEntry *data, uint32_t str_addr) {
    // PS map update
    address addr;
    if (data->has_spatial)
        addr = data->spatial->last_address();
    else
        addr = data->temporal;
    if (ps_map.find(addr) == ps_map.end()) {
        ps_map[addr] = PSEntry();
        ps_map[addr].set(str_addr);
    } else {
        ps_map[addr].set(str_addr);
    }

    // SP map update
    if (sp_map.find(str_addr) == sp_map.end()) {
        sp_map[str_addr] = SPEntry();
        sp_map[str_addr].set(data);
    } else {
        sp_map[str_addr].set(data);
    }
}

}

