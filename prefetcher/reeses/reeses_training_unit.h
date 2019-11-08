#ifndef REESES_TRAINING_UNIT_H
#define REESES_TRAINING_UNIT_H

#include "reeses_spatial.h"

namespace reeses {

struct TUEntry {
    address temporal;
    uint32_t conf;
    bool has_spatial;
    SpatialPattern *spatial;

    TUEntry *clone() const {
        TUEntry *result = new TUEntry();
        result->temporal = temporal;
        result->conf = conf;
        result->has_spatial = has_spatial;
        if (spatial != nullptr) {
            result->spatial = spatial->clone();
        }
        return result;
    }

    void inc() {
        if (conf != MAX_CONF)
            conf++;
    }

    bool dec() {
        if (conf != 0)
            conf--;
        return !conf;
    }

    bool operator==(const TUEntry &other) const {
        if (other.has_spatial) {
            return has_spatial && (*spatial == *(other.spatial));
        } else {
            return !has_spatial && temporal == other.temporal;
        }
    }
        
    ~TUEntry() {
        if (spatial != nullptr)
            delete spatial;
    }

    TUEntry() :
        temporal(0), conf(INIT_CONF), has_spatial(false), spatial(nullptr) {}
    TUEntry(address addr) :
        temporal(addr), conf(INIT_CONF), has_spatial(false), spatial(nullptr) {}
};

/* similar to ISB's training unit
 * keeps tracker of past accesses by PC to create correlations */
struct TrainingUnit {
    // stores mapping between each PC and its history
    map<pc, TUEntry*> data;
    map<pc, uint32_t> spatial_counters;
    map<pc, bool> last_spatial;
    bool FOOTPRINT;

    void inc_spatial(pc cur_pc) {
        if (spatial_counters.find(cur_pc) == spatial_counters.end())
            spatial_counters[cur_pc] = 0;
        uint32_t &counter = spatial_counters[cur_pc];
        counter += SPATIAL_INC;
        if (counter > SPATIAL_MAX)
            counter = SPATIAL_MAX;
    }

    void dec_spatial(pc cur_pc) {
        if (spatial_counters.find(cur_pc) == spatial_counters.end())
            spatial_counters[cur_pc] = 0;
        uint32_t &counter = spatial_counters[cur_pc];
        if (counter < SPATIAL_DEC) counter = 0;
        else counter -= SPATIAL_DEC;
    }

    TrainingUnit(bool f) :
        FOOTPRINT(f) {}

    TrainingUnit() {}

    address last_address(pc cur_pc) {
        if (data.find(cur_pc) == data.end()) {
            return 0;
        } else {
            if (data[cur_pc]->has_spatial) {
                return data[cur_pc]->spatial->last_address();
            } else {
                return data[cur_pc]->temporal;
            }
        }
    }

    /* updates the PC's history and returns the old history, if evicted */
    TUEntry *update(pc cur, address addr_B) {
        TUEntry *result = nullptr;
        if (data.find(cur) == data.end()) {
            // this is a new PC
            data[cur] = new TUEntry(addr_B);
            last_spatial[cur] = false;
        } else if (data[cur]->has_spatial) {
            // existing spatial pattern
            SpatialPattern *existing = data[cur]->spatial;
            if (existing->last_address() == addr_B)
                return nullptr;

            if (existing->matches(addr_B)) {
                // new addr matches old pattern
                existing->add(addr_B);
            } else {
                // new addr doesn't match old pattern
                result = data[cur];
                data[cur] = new TUEntry(addr_B);
            }
        } else {
            // no existing spatial pattern
            address last_addr = data[cur]->temporal;
            if (last_addr == addr_B)
                return nullptr;

            address prev_reg = last_addr >> LOG2_REGION_SIZE;
            address new_reg = addr_B >> LOG2_REGION_SIZE;
            int32_t delta = addr_B-last_addr;

            if (!NO_SPATIAL && FOOTPRINT && prev_reg == new_reg) {
                // creating a new Footprint
                data[cur]->spatial = new Footprint(last_addr);
                data[cur]->spatial->add(addr_B);
                data[cur]->has_spatial = true;
            } else if (!NO_SPATIAL && !FOOTPRINT && delta >= -REGION_SIZE && delta < REGION_SIZE) {
                // creating a new delta pattern
                data[cur]->spatial = new DeltaPattern(delta, addr_B);
                data[cur]->has_spatial = true;
            } else {
                // kicking out old temporal
                result = data[cur];
                data[cur] = new TUEntry(addr_B);
            }
        }

        if (result != nullptr) {
            if (last_spatial[cur] && result->has_spatial) {
                inc_spatial(cur);
            } else {
                dec_spatial(cur);
            }
            last_spatial[cur] = result->has_spatial;
        }
        return result;
    }
};

}

#endif
