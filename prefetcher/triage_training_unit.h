#ifndef TRIAGE_TRAINING_UNIT_H__
#define TRIAGE_TRAINING_UNIT_H__

#include <stdint.h>
#include <map>

class TriageConfig;

struct TriageTrainingUnitEntry {
    uint64_t addr;
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
        bool get_last_addr(uint64_t pc, uint64_t &prev_addr);
        void set_addr(uint64_t pc, uint64_t addr);
};

#endif // TRIAGE_TRAINING_UNIT_H__
