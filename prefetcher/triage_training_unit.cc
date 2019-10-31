
#include <assert.h>
#include "triage_training_unit.h"
#include "triage.h"

using namespace std;

TriageTrainingUnit::TriageTrainingUnit() {
    current_timer = 0;
}

void TriageTrainingUnit::set_conf(TriageConfig* conf) {
    max_size = conf->training_unit_size;
}

Metadata TriageTrainingUnit::set_addr(uint64_t pc, uint64_t addr) {
    auto it = entry_list.find(pc);
    Metadata result;
    if (it != entry_list.end()) {
        // pc exists already
        TriageTrainingUnitEntry &entry = it->second;
        entry.timer = current_timer++;
        int32_t delta = addr - entry.trigger_addr;
        uint64_t last_addr = entry.in_spatial ? entry.cur_spatial.last_addr : entry.trigger_addr;
        if (last_addr == addr) {
            // ignore repeated addresses
        } else if (entry.in_spatial) {
            // already in spatial
            if (entry.cur_spatial.matches(addr)) {
                // continuing existing spatial pattern
                entry.cur_spatial.add(addr);
            } else {
                // evicting old spatial pattern
                result.set_spatial(entry.trigger_addr, entry.cur_spatial);
                entry.in_spatial = false;
                entry.trigger_addr = addr;
            }
        } else if (delta >= -MAX_DELTA && delta < MAX_DELTA && 
                  (addr >> LOG2_REGION_SIZE == entry.trigger_addr >> LOG2_REGION_SIZE)) {
            // creating new spatial
            entry.in_spatial = true;
            entry.cur_spatial = DeltaPattern(delta, addr);
        } else {
            // just another temporal
            result.set_addr(entry.trigger_addr);
            entry.trigger_addr = addr;
        }
    } else {
        // this pc does not exist yet
        if (entry_list.size() == max_size) 
            evict();
        entry_list[pc].in_spatial = false;
        entry_list[pc].trigger_addr = addr;
        entry_list[pc].timer = current_timer++;
    }
    assert(entry_list.size() <= max_size);
    return result;
}

void TriageTrainingUnit::evict() {
    assert(entry_list.size() == max_size);
    map<uint64_t, TriageTrainingUnitEntry>::iterator it, min_it;
    uint64_t min_timer = current_timer;
    for (it = entry_list.begin(); it != entry_list.end(); it++) {
        assert(it->second.timer < current_timer);
        if (it->second.timer < min_timer) {
            min_it = it;
            min_timer = it->second.timer;
        }
    }

    entry_list.erase(min_it);
}

