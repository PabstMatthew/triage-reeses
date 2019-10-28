#ifndef __MEM_CACHE_PREFETCH_ISB_TRAINING_UNIT_HH__
#define __MEM_CACHE_PREFETCH_ISB_TRAINING_UNIT_HH__

#include <map>

typedef enum training_unit_repl_type_e {
    ISB_TU_REPL_TYPE_LRU,
    ISB_TU_REPL_TYPE_PERFECT
} training_unit_repl_type_t;

struct TrainingUnitEntry
{
    uint64_t key;
    uint64_t addr;
    uint32_t str_addr;

    TrainingUnitEntry(){
        reset();
    }

    void reset(){
        key = 0;
        addr = 0;
        str_addr = 0;
    }

    TrainingUnitEntry(uint64_t _key){
        key = _key;
        addr = 0;
        str_addr = 0;
    }
};

class TrainingUnitRepl
{
};

class TrainingUnit
{
    TrainingUnitRepl repl;
    std::map<uint64_t, TrainingUnitEntry*> content;
    unsigned max_size;

    public:
        uint64_t training_unit_pc_not_found, training_unit_pc_found;

        TrainingUnit();

        bool access(uint64_t key, uint64_t& addr_A, uint32_t& str_addr_A, uint64_t addr_B);
        void update(uint64_t key, uint64_t phy_addr, uint32_t str_addr);
};


#endif // __MEM_CACHE_PREFETCH_ISB_TRAINING_UNIT_HH__

