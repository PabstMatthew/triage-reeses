#ifndef TRIAGE_TRAINING_UNIT_H__
#define TRIAGE_TRAINING_UNIT_H__

#include <stdint.h>
#include <map>
#include "reeses_spatial.h"

class TriageConfig;

struct Metadata {
    bool valid;
    bool spatial;
    DeltaPattern next_spatial;
    uint64_t addr;

    Metadata() : valid(false) {}

    void set_addr(uint64_t next_addr) {
        valid = true;
        spatial = false;
        addr = next_addr;
    }

    void set_spatial(uint64_t trigger, DeltaPattern dp) {
        valid = true;
        addr = trigger;
        spatial = true;
        next_spatial = dp;
    }
    
    bool operator==(const Metadata& other) const {
        return (spatial && other.spatial && next_spatial == other.next_spatial)
                || (!spatial && !other.spatial && addr == other.addr);
    }

    bool operator!=(const Metadata& other) const { return !(*this == other); }
};

struct TriageTrainingUnitEntry {
    // are we in the middle of training a spatial pattern?
    bool in_spatial;
    // stores the trigger address for a spatial pattern, 
    // or simply the last address
    uint64_t trigger_addr;
    // stores the current spatial pattern
    DeltaPattern cur_spatial;
    // used for LRU purposes
    uint64_t timer;
};

class TriageTrainingUnit {
    // XXX Only support fully associative LRU for now
    // PC->TrainingUnitEntry
    std::map<uint64_t, TriageTrainingUnitEntry> entry_list;
    uint64_t current_timer;
    uint64_t max_size;

    void evict();

    public:
        TriageTrainingUnit();
        void set_conf(TriageConfig* conf);
        Metadata set_addr(uint64_t pc, uint64_t addr);
};

#endif // TRIAGE_TRAINING_UNIT_H__
