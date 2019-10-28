#ifndef __TRIAGE_ONCHIP_H__
#define __TRIAGE_ONCHIP_H__

#include <vector>
#include <map>
#include <set>
#include <stdint.h>
#include "optgen_simple.h"
#include "isb_hawkeye_predictor.h"

#define ONCHIP_LINE_SIZE 1
#define ONCHIP_LINE_SHIFT 0
#define INVALID_ADDR 0xdeadbeef

struct TriageConfig;

enum TriageReplType {
    TRIAGE_REPL_LRU,
    TRIAGE_REPL_HAWKEYE,
    TRIAGE_REPL_PERFECT
};

struct TriageOnchipEntry {
    uint64_t next_addr[ONCHIP_LINE_SIZE];
    int confidence[ONCHIP_LINE_SIZE];
    bool valid[ONCHIP_LINE_SIZE];
    // Used for replacement policy, it is rrpv for rrpv-based replacement
    // policies, but can be used for other usages (like frequency in LFU）
    uint64_t rrpv;

    TriageOnchipEntry();
    void increase_confidence(unsigned);
    void decrease_confidence(unsigned);
    void init();
};

class TriageRepl {
    protected:
        std::vector<std::map<uint64_t, TriageOnchipEntry> > *entry_list;
        TriageReplType type;

    public:
        TriageRepl(std::vector<std::map<uint64_t, TriageOnchipEntry> >* entry_list);
        virtual void addEntry(uint64_t set_id, uint64_t addr, uint64_t pc) = 0;
        virtual uint64_t pickVictim(uint64_t set_id) = 0;
        virtual void print_stats() {}
        virtual uint32_t get_assoc() { return 8; }

        static TriageRepl* create_repl(std::vector<std::map<uint64_t, TriageOnchipEntry> >* entry_list,
                TriageReplType type, uint64_t assoc, bool use_dynamic_assoc);
};

class TriageReplLRU : public TriageRepl
{
    public:
        TriageReplLRU(std::vector<std::map<uint64_t, TriageOnchipEntry> >* entry_list);
        void addEntry(uint64_t set_id, uint64_t addr, uint64_t pc);
        uint64_t pickVictim(uint64_t set_id);
};

// Two sampled optgen: assoc of 4 and 8.
// Three choiese: 0, 4, and 8
#define HAWKEYE_SAMPLE_ASSOC_COUNT 2
#define HAWKEYE_EPOCH_LENGTH 1000
extern unsigned hawkeye_sample_assoc[];
class TriageReplHawkeye : public TriageRepl
{
    unsigned max_rrpv;
    std::vector<uint64_t> optgen_mytimer;
//    std::vector<OPTgen> optgen;
    unsigned dynamic_optgen_choice;
    std::vector<std::vector<OPTgen> > sample_optgen;
    std::map<uint64_t, ADDR_INFO> optgen_addr_history;
    std::map<uint64_t, uint64_t> signatures;
    std::map<uint64_t, uint32_t> hawkeye_pc_ps_hit_predictions;
    std::map<uint64_t, uint32_t> hawkeye_pc_ps_total_predictions;
    IsbHawkeyePCPredictor predictor;
    uint64_t last_access_count, curr_access_count;
    bool use_dynamic;

    void choose_optgen();

    public:
        TriageReplHawkeye(std::vector<std::map<uint64_t, TriageOnchipEntry> >* entry_list, uint64_t assoc, bool use_dynamic_assoc);
        void addEntry(uint64_t set_id, uint64_t addr, uint64_t pc);
        uint64_t pickVictim(uint64_t set_id);
        uint32_t get_assoc();

        void print_stats();
};

class TriageReplPerfect : public TriageRepl
{
    public:
        TriageReplPerfect(std::vector<std::map<uint64_t, TriageOnchipEntry> >* entry_list);
        void addEntry(uint64_t set_id, uint64_t addr, uint64_t pc);
        uint64_t pickVictim(uint64_t set_id);
};

class TriageOnchip {
    uint32_t num_sets, assoc;
    uint64_t index_mask;
    std::vector<std::map<uint64_t, TriageOnchipEntry> > entry_list;
    TriageReplType repl_type;
    TriageRepl *repl;
    bool use_dynamic_assoc;

    uint64_t get_set_id(uint64_t addr);
    uint64_t get_line_offset(uint64_t addr);

    public:
        TriageOnchip();
        void set_conf(TriageConfig *config);

        void update(uint64_t prev_addr, uint64_t next_addr, uint64_t pc, bool update_repl);
        bool get_next_addr(uint64_t prev_addr, uint64_t &next_addr, uint64_t pc, bool update_stats);
        int increase_confidence(uint64_t addr);
        int decrease_confidence(uint64_t addr);

        void print_stats();
        uint32_t get_assoc();
};

#endif // __TRIAGE_ONCHIP_H__

