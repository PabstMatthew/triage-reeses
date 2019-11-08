#ifndef REESES_OFFCHIP_H
#define REESES_OFFCHIP_H

#include "reeses_types.h"
#include "reeses_training_unit.h"

using namespace std;

namespace reeses {

struct PSEntry {
    uint64_t last_access;
    uint32_t str_addr;
    bool valid;
    bool cached;

    PSEntry() { reset(); }

    void reset() {
        last_access = 0;
        str_addr = 0;
        valid = false;
        cached = false;
    }

    void set(uint32_t addr) {
        last_access = 0;
        str_addr = addr;
        valid = true;
    }
};

struct SPEntry {
    uint64_t last_access;
    TUEntry *data;
    uint32_t conf;
    bool valid;
    bool dirty;
    bool cached;

    SPEntry() : data(nullptr) { reset(); }

    void reset() {
        last_access = 0;
        if (data != nullptr)
            delete data;
        data = nullptr;
        conf = 0;
        valid = false;
        dirty = false;
        cached = false;
    }

    void set(TUEntry *new_data) {
        reset();
        data = new_data;
        conf = INIT_CONF;
        valid = true;
    }

    void inc() {
        conf = (conf == MAX_CONF) ? conf : conf+1;
    }

    bool dec() {
        conf = (conf == 0) ? conf : conf-1;
        return conf;
    }
};

typedef enum off_chip_request_type {
    OCI_REQ_LOAD_SP,
    OCI_REQ_LOAD_PS,
    OCI_REQ_STORE,
} ocrt_t;

class OffChipInfo {
    public:
        bool get_structural_address(address addr, uint32_t& str_addr);
        bool get_physical_data(TUEntry* &data, uint32_t str_addr);
        void update(TUEntry *data, uint32_t str_addr);
    private:
        map<address, PSEntry> ps_map;
        map<uint32_t, SPEntry> sp_map;
};

}

#endif
