#ifndef REESES_METADATA_CACHE_H
#define REESES_METADATA_CACHE_H

#include "reeses_types.h"
#include "reeses_training_unit.h"

using namespace std;

namespace reeses {

/* stores correlations between temporal and spatial components */
struct metadata_cache {
    map<pc, map<address, TUEntry*>> metadata;
    map<address, TUEntry*> lazy_metadata;

    /* main training algorithm */
    bool train_metadata(map<address, TUEntry*> &target, address trigger, TUEntry *data) {
        bool result = false;

        // metadata for exact trigger
        if (target.find(trigger) != target.end()) {
            // PC+addr metadata training
            if (*(target[trigger]) == *data) {
                 // positively traing on matches
                 target[trigger]->inc();
            } else {
                // detrain on divergence
                if (target[trigger]->dec()) {
                    delete target[trigger];
                    target[trigger] = data->clone();
                }
                result = true;
            }
        } else {
            // insert on first-time triggers
            target[trigger] = data->clone();
        }

        // no offset prediction for temporal entries
        if (!data->has_spatial)
            return result;

        // metadata for offset prediction
        address offset = trigger % REGION_SIZE;
        if (target.find(offset) != target.end()) {
            if (*(target[offset]) == *data) {
                // positively train on matches
                target[offset]->inc();
            } else if (target[offset]->dec()) {
                delete target[offset];
                target[offset] = data->clone();
            }
        } else {
            target[offset] = data->clone();
        }
        return result;
    }

    /* adds a correlated pair to the cache;
     * returns true if data doesn't match existing mapping */
    bool train(pc cur_pc, address trigger, TUEntry *data) {
        // initialize maps
        if (metadata.find(cur_pc) == metadata.end()) {
            metadata[cur_pc] = map<address, TUEntry*>();
        }

        train_metadata(lazy_metadata, trigger, data);
        return train_metadata(metadata[cur_pc], trigger, data);
    }

    /* given a PC and address, predicts the next stream entry
     * first checks PC+addr, then checks addr */
    TUEntry *predict_next(pc cur_pc, address trigger) {
        if (metadata.find(cur_pc) != metadata.end() && 
                metadata[cur_pc].find(trigger) != metadata[cur_pc].end()) {
            // check for matching PC+addr first
            return metadata[cur_pc][trigger];
        } else if (lazy_metadata.find(trigger) != lazy_metadata.end()) {
            // next check for matching addr
            return lazy_metadata[trigger];
        } else {
            return nullptr;
        }
    }

    /* generates a list of prefetches given a trigger and base adddress */
    vector<address> predict(pc cur_pc, address trigger, address last_addr, size_t dist) {
        vector<address> result = vector<address>();
        if (metadata.find(cur_pc) == metadata.end())
            return result;
        
        while (result.size() < dist) {
            // get the next prediction
            TUEntry *prediction = predict_next(cur_pc, trigger);
            if (prediction == nullptr) {
                // attempt to make offset prediction
                address offset = trigger % REGION_SIZE;
                if ((trigger >> LOG2_REGION_SIZE) != (last_addr >> LOG2_REGION_SIZE) &&
                        metadata[cur_pc].find(offset) != metadata[cur_pc].end()) {
                    // only make offset prediction if we enter a new region
                    prediction = metadata[cur_pc][offset];
                }
            }

            // no correlations found
            if (prediction == nullptr)
                return result;

            // add the new prediction(s)
            if (prediction->has_spatial) {
                SpatialPattern *pattern = prediction->spatial;
                vector<address> preds = pattern->predict(trigger);
                // if we don't return for this case, we will infinitely loop
                if (preds.size() == 0)
                    return result;
                result.insert(result.end(), preds.begin(), preds.end());
            } else {
                result.push_back(prediction->temporal);
            }
            // recursively predict addresses using the last address predicted
            last_addr = trigger;
            trigger = result.back();
        }
        return result;
    }
};

}

#endif
