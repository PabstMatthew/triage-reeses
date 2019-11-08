#ifndef REESES_OFFSET_CACHE_H
#define REESES_OFFSET_CACHE_H

#include "reeses_types.h"
#include "reeses_config.h"
#include "reeses_training_unit.h"

using namespace std;

namespace reeses {

struct OCEntry {
    OCEntry() : data(nullptr) { reset(); }

    void reset() {
        last_access = 0;
        if (data != nullptr)
            delete data;
        data = nullptr;
    }

    void set(TUEntry *new_data) {
        reset();
        data = new_data;
    }

    uint64_t last_access;
    TUEntry *data;
};

class OffsetCache {
    public:
        OffsetCache();
        bool lookup(pc cur_pc, address offset, TUEntry* &tu_entry);
        void insert(pc cur_pc, address offset, TUEntry *tu_entry);
    private:
        map<pc, OCEntry> offset_cache[OC_SETS];
        uint64_t cur_timestamp;
};

}

#endif
