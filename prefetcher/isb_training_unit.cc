
#include <iostream>
#include <cassert>

#include "isb_training_unit.h"

using namespace std;

TrainingUnit::TrainingUnit()
{
    training_unit_pc_found = 0;
    training_unit_pc_not_found = 0;
}

bool TrainingUnit::access(uint64_t pc, uint64_t& last_phy_addr,
        uint32_t& last_str_addr, uint64_t next_addr)
{
    bool pair_found = true;
    if (content.find(pc) == content.end())
    {
        TrainingUnitEntry* new_training_entry = new TrainingUnitEntry(pc);
        assert(new_training_entry);
        new_training_entry->reset();
        content[pc] = new_training_entry;
        ++training_unit_pc_not_found;
        pair_found = false;
    }
    ++training_unit_pc_found;

    assert(content.find(pc) != content.end());
    TrainingUnitEntry* curr_training_entry = content.find(pc)->second;

    assert(curr_training_entry != NULL);
    last_str_addr = curr_training_entry->str_addr;
    last_phy_addr = curr_training_entry->addr;
    uint64_t last_addr = curr_training_entry->addr;
    if (last_addr == next_addr)
        return false;
    return pair_found;
}

void TrainingUnit::update(uint64_t pc, uint64_t addr, uint32_t str_addr)
{
    assert(content.find(pc) != content.end());
    TrainingUnitEntry* curr_training_entry = content.find(pc)->second;
    assert(curr_training_entry);
    curr_training_entry->addr = addr;
    curr_training_entry->str_addr = str_addr;
}

