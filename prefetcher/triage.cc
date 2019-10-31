
#include <assert.h>
#include <iostream>

#include "triage.h"

using namespace std;

//#define DEBUG

#ifdef DEBUG
#define debug_cout cerr << "[TABLEISB] "
#else
#define debug_cout if (0) cerr
#endif

Triage::Triage() {
    trigger_count = 0;
    predict_count = 0;
    same_addr = 0;
    new_addr = 0;
    no_next_addr = 0;
    conf_dec_retain = 0;
    conf_dec_update = 0;
    conf_inc = 0;
    new_stream = 0;
    total_assoc = 0;
    spatial = 0;
    temporal = 0;
}

void Triage::set_conf(TriageConfig *config) {
    lookahead = config->lookahead;
    degree = config->degree;

    training_unit.set_conf(config);
    on_chip_data.set_conf(config);
}

void Triage::train(uint64_t pc, uint64_t addr, bool cache_hit) {
    if (cache_hit) {
        training_unit.set_addr(pc, addr);
        return;
    }
    Metadata new_entry = training_unit.set_addr(pc, addr);
    if (new_entry.valid) {
        bool is_spatial = new_entry.spatial;
        if (!is_spatial && new_entry.addr == addr) {
            // Same Addr
            debug_cout << hex << "Same Addr: " << new_entry.addr << ", " << addr <<endl;
            same_addr++;
        } else {
            // New Addr
            new_addr++;

            if (is_spatial) {
                spatial += new_entry.next_spatial.size();
            } else {
                temporal++;
            }
            
            Metadata next_entry = on_chip_data.get_next_entry(addr, pc, false);
            if (!next_entry.valid) {
                on_chip_data.update(addr, new_entry, pc, true);
                no_next_addr++;
            } else if (next_entry != new_entry) {
                int conf = on_chip_data.decrease_confidence(addr);
                conf_dec_retain++;
                if (conf == 0) {
                    conf_dec_update++;
                    on_chip_data.update(addr, new_entry, pc, false);
                }
            } else {
                on_chip_data.increase_confidence(addr);
                conf_inc++;
            }
        }
    } else {
        // New Stream
        debug_cout << hex << "StreamHead: " << addr <<endl;
        new_stream++;
    }
}

void Triage::predict(uint64_t pc, uint64_t addr, bool cache_hit) {
    Metadata next_entry = on_chip_data.get_next_entry(addr, pc, false);
    if (next_entry.valid) {
        if (next_entry.spatial) {
            for (uint64_t pred : next_entry.next_spatial.predict(addr)) {
                debug_cout << hex << "Predict: " << addr << " " << pred << dec << endl;
                predict_count++;
                next_addr_list.push_back(pred);
                assert(pred != addr);
            }
        } else {
            uint64_t next_addr = next_entry.addr;
            debug_cout << hex << "Predict: " << addr << " " << next_addr << dec << endl;
            predict_count++;
            next_addr_list.push_back(next_addr);
            assert(next_addr != addr);
        }
    }
}

void Triage::calculatePrefetch(uint64_t pc, uint64_t addr, bool cache_hit, uint64_t *prefetch_list, int max_degree, uint64_t cpu) {
    // XXX Only allow lookahead = 1 and degree=1 for now
    assert(lookahead == 1);
    assert(degree == 1);

    assert(degree <= max_degree);
    
    if (pc == 0) return; //TODO: think on how to handle prefetches from lower level

    debug_cout << hex << "Trigger: pc: " << pc << ", addr: " << addr << dec << " " << cache_hit << endl;

    next_addr_list.clear();
    trigger_count++;
    total_assoc += get_assoc();

    // Predict
    predict(pc, addr, cache_hit);

    // Train
    train(pc, addr, cache_hit);

    for (size_t i = 0; i < next_addr_list.size(); i++)
        prefetch_list[i] = next_addr_list[i];
}

uint32_t Triage::get_assoc() {
    return on_chip_data.get_assoc();
}

void Triage::print_stats() {
    cout << dec << "trigger_count=" << trigger_count <<endl;
    cout << "predict_count=" << predict_count <<endl;
    cout << "same_addr=" << same_addr <<endl;
    cout << "new_addr=" << new_addr <<endl;
    cout << "new_stream=" << new_stream <<endl;
    cout << "no_next_addr=" << no_next_addr <<endl;
    cout << "conf_dec_retain=" << conf_dec_retain <<endl;
    cout << "conf_dec_update=" << conf_dec_update <<endl;
    cout << "conf_inc=" << conf_inc <<endl;
    cout << "total_assoc=" << total_assoc <<endl;
    cout << "spatial=" << spatial << endl;
    cout << "temporal=" << temporal << endl;

    on_chip_data.print_stats();
}

