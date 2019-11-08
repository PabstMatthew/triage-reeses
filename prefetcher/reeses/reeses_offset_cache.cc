#include "reeses_offset_cache.h"

using namespace std;

namespace reeses {

OffsetCache::OffsetCache() : cur_timestamp(0) {}

bool OffsetCache::lookup(pc cur_pc, address addr, TUEntry* &tu_entry) {
    cur_timestamp++;

    address offset = addr % REGION_SIZE;
    map<pc, OCEntry> &oc_set = offset_cache[offset];
    if (oc_set.find(cur_pc) == oc_set.end()) {
        // no entry under this PC
        D(cout << "\t\t\tfound no entry for offset " << offset << endl;)
        return false;
    } else {
        // found entry
        D(cout << "\t\t\tfound entry for offset " << offset << endl;)
        tu_entry = oc_set[cur_pc].data;
        oc_set[cur_pc].last_access = cur_timestamp;
        return true;
    }
}
void OffsetCache::insert(pc cur_pc, address addr, TUEntry *tu_entry) {
    assert(tu_entry != nullptr);
    cur_timestamp++;

    address offset = addr % REGION_SIZE;
    map<pc, OCEntry> &oc_set = offset_cache[offset];
    D(cout << "\t\t\tinserting offset pattern with offset " << offset << endl;)
    if (oc_set.find(cur_pc) == oc_set.end()) {
        // no entry exists for this PC yet
        if (oc_set.size() < OC_WAYS) {
            // room left in this set
            D(cout << "\t\t\tno eviction necessary" << endl;)
            oc_set[cur_pc] = OCEntry();
            oc_set[cur_pc].set(tu_entry);
            oc_set[cur_pc].last_access = cur_timestamp;
        } else {
            if (!IDEAL_OC) {
                // no room left in this set (time to evict)
                D(cout << "\t\t\tevicting an entry" << endl;)
                pc lru_key = 0;
                uint64_t lru_pos = -1;
                for (auto const &entry : oc_set) {
                    if (entry.second.last_access < lru_pos) {
                        lru_pos = entry.second.last_access;
                        lru_key = entry.first;
                    }
                }
                oc_set.erase(lru_key);
                oc_set[cur_pc] = OCEntry();
            }
            oc_set[cur_pc].set(tu_entry);
            oc_set[cur_pc].last_access = cur_timestamp;
        }
    } else {
        // entry already exists for this PC
        oc_set[cur_pc].set(tu_entry);
        oc_set[cur_pc].last_access = cur_timestamp;
    }
}

}

