#include "reeses_stream.h"

namespace reeses {

void PrefetchStream::update(address addr) {
    size_t left = remaining(addr);
    bool in_stream = left != stream_buffer.size();
    size_t index = (!in_stream) ? 0 : stream_buffer.size()-left-1;

    // purge old candidates
    if (in_stream) {
        stream_buffer.erase(stream_buffer.begin(), stream_buffer.begin()+index);
    } else {
        stream_buffer.clear();
    }

    // add new candidates
    if (!in_stream) {
        // if not in stream, start fetching new stream
        predict_upstream(addr, LOOKAHEAD); 
    } else if (left < LOOKAHEAD) {
        // if in stream and running out of candidates, fetch more
        address future_addr = stream_buffer.back().addr;
        predict_upstream(future_addr, LOOKAHEAD-left);
    }

    // prefetch, if necessary
    prefetch(index);

    // update internals
    last_addr = addr;
}

/* returns the number of entries left in the stream */
size_t PrefetchStream::remaining(address addr) {
    size_t q_size = stream_buffer.size();
    for (size_t i = 0; i < q_size; i++)
        if (addr == stream_buffer[i].addr)
            return q_size-i-1;
    return q_size;
}

/* adds new prefetch candidates to the stream */
void PrefetchStream::predict_upstream(address addr, size_t dist) {
    address last_seen_addr = last_addr;
    // correct last address if fetching from end of stream
    if (dist < LOOKAHEAD && stream_buffer.size() > 1)
        last_seen_addr = 0; // need to this to trigger offset prefetching
        //last_seen_addr = stream_buffer[stream_buffer.size()-2].addr;

    // generate predictions
    vector<address> candidates = on_chip_info->predict(id, addr, last_seen_addr, dist);
    for (address candidate : candidates) {
        stream_buffer.push_back(StreamEntry(candidate));
    }
}

/* prefetches ahead of the current stream position, if possible */
void PrefetchStream::prefetch(size_t index) {
    for (size_t i = index; i < (index+LOOKAHEAD) && i < stream_buffer.size(); i++) {
        StreamEntry &cur = stream_buffer.at(i);
        if (!cur.issued) {
            address target = cur.addr << LOG2_BLOCK_SIZE;
            if (cache->prefetch_line(id, target, target, FILL_LLC, 0))
                prefetcher->stats["reeses_issues"] += 1;
            cur.issued = true;
        }
    }
}

}
