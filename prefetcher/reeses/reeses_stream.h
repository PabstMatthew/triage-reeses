#ifndef REESES_STREAM_H
#define REESES_STREAM_H

#include "reeses_types.h"
#include "reeses_config.h"
#include "reeses_onchip.h"

namespace reeses {

class OnChipInfo;
class ReesesPrefetcher;

struct PrefetchStream {
    struct StreamEntry {
        address addr;
        bool issued;

        StreamEntry(address a) : addr(a), issued(false) {}
        StreamEntry() {}
    };

    pc id;
    address last_addr;
    deque<StreamEntry> stream_buffer;
    CACHE *cache;
    OnChipInfo *on_chip_info;
    TrainingUnit *tu;
    ReesesPrefetcher *prefetcher;

    PrefetchStream(pc i, CACHE *c, OnChipInfo *mc, TrainingUnit *tu, ReesesPrefetcher *pf) :
        id(i), last_addr(0), cache(c), on_chip_info(mc), tu(tu), prefetcher(pf) { 
            stream_buffer = deque<StreamEntry>();
    }
    PrefetchStream() {}

    void update(address addr);

    /* helper methods */
    size_t remaining(address addr);
    void predict_upstream(address addr, size_t dist);
    void prefetch(size_t index);
};

}

#endif
