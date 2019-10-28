#ifndef __TRIAGE_H__
#define __TRIAGE_H__

#include <iostream>
#include <map>
#include <vector>

#include "champsim.h"
#include "triage_training_unit.h"
#include "triage_onchip.h"

struct TriageConfig {
    int lookahead;
    int degree;

    int on_chip_set, on_chip_assoc;
    int training_unit_size;
    bool use_dynamic_assoc;

    TriageReplType repl;
};

class Triage {
    TriageTrainingUnit training_unit;

    int lookahead, degree;

    void train(uint64_t pc, uint64_t addr, bool hit);
    void predict(uint64_t pc, uint64_t addr, bool hit);

    // Stats
    uint64_t same_addr, new_addr, new_stream;
    uint64_t no_next_addr, conf_dec_retain, conf_dec_update, conf_inc;
    uint64_t predict_count, trigger_count;
    uint64_t total_assoc;

    std::vector<uint64_t> next_addr_list;

    public:
    TriageOnchip on_chip_data;
        Triage();
        void set_conf(TriageConfig *config);
        void calculatePrefetch(uint64_t pc, uint64_t addr,
                bool cache_hit, uint64_t *prefetch_list,
                int max_degree, uint64_t cpu);
        void print_stats();
        uint32_t get_assoc();
};

#endif // __TRIAGE_H__
