#ifndef SPATIAL_H
#define SPATIAL_H

#include "reeses_types.h"

// size of regions (for Footprints) in cache blocks, also used as max delta length
#define REGION_SIZE 64
#define LOG2_REGION_SIZE 6

#define MAX_DELTA 32
#define MAX_DELTA_LENGTH 64

using namespace std;

/* interface for spatial patterns;
 * only one instance of this class should exist per binary */
struct SpatialPattern {
    /* determines if a new miss address continues the pattern */
    virtual bool matches(address) = 0;
    /* adds a new miss address to the pattern */
    virtual void add(uint64_t) = 0;
    /* returns how many addresses are represented by this pattern */
    virtual uint32_t size() = 0;
    /* returns the last address seen by the pattern */
    virtual address last_address() = 0;
    virtual bool operator==(const SpatialPattern&) const = 0;
    /* predicts a list of prefetches based on a trigger */
    virtual vector<address> predict(address) = 0;
    /* creates a clone of the spatial pattern */
    virtual SpatialPattern *clone() const = 0;
    virtual ~SpatialPattern() {};
};

/* a spatial pattern defined by a delta and a length
 * "simple" deltas only */
struct DeltaPattern : public SpatialPattern {
    int32_t delta;
    uint32_t length;
    address last_addr;

    DeltaPattern(int32_t d, address l) :
        delta(d), length(1), last_addr(l) {}

    DeltaPattern() :
        delta(0), length(0), last_addr(0) {}

    ~DeltaPattern() {}

    virtual bool matches(address addr_B) override {
        int32_t last_delta = addr_B - last_addr;
        return last_delta == delta && 
                length < MAX_DELTA_LENGTH &&
                (addr_B >> LOG2_REGION_SIZE) == (last_addr >> LOG2_REGION_SIZE);
    }

    virtual void add(address addr_B) override {
        if (!matches(addr_B))
            throw invalid_argument("adding delta that does not match");
        last_addr = addr_B;
        length++;
    }

    virtual uint32_t size() override { return length; }

    virtual address last_address() override { return last_addr; }

    virtual bool operator==(const SpatialPattern &other) const override {
        /* casting allowed here because only one type of SpatialPattern
         * can be instantiated in a binary */ 
        DeltaPattern other_delta = *((DeltaPattern*) ((void*) &other));
        return delta == other_delta.delta && length == other_delta.length;
    }

    virtual vector<address> predict(address trigger) override {
        vector<address> result;
        for (int32_t i = 1; i <= (int32_t) length; i++) {
            result.push_back(trigger+delta*i);
        }
        return result;
    }

    virtual SpatialPattern *clone() const override {
        DeltaPattern *result = new DeltaPattern();
        result->delta = delta;
        result->length = length;
        result->last_addr = last_addr;
        return result;
    }

    friend ostream& operator <<(ostream &os, const DeltaPattern &other) {
        os << "delta " << other.delta << " with length " << other.length;
        return os;
    }
};

/* a spatial pattern defined by a bitmap of accessed lines in a region
 * only temporally ordered by forwards/backwards traversal */
struct Footprint : public SpatialPattern {
    bool bitmap[REGION_SIZE];
    address base;
    address last_addr;
    uint32_t start;
    uint32_t length;
    bool reverse;

    Footprint(address addr_B) {
        for (uint32_t i = 0; i < REGION_SIZE; i++) {
            bitmap[i] = false;
        }
        base = addr_B >> LOG2_REGION_SIZE;
        start = addr_B % REGION_SIZE;
        length = 0;
        reverse = false;
        add(addr_B);
    }

    Footprint() {}
    ~Footprint() {}

    virtual bool matches(address addr_B) override {
        return addr_B >> LOG2_REGION_SIZE == base;
    }

    virtual void add(address addr_B) override {
        uint32_t index = addr_B % REGION_SIZE;

        // check if this bit has been set yet
        if (!bitmap[index]) {
            length++;
            bitmap[index] = true;

            // check if new address is forwards or backwards in memory
            if (index < start) {
                reverse = true;
            }
            last_addr = addr_B;
        }
    }

    virtual uint32_t size() override { return length-1; }
    virtual address last_address() override { return last_addr; }

    virtual vector<address> predict(address trigger) override {
        vector<address> result;
        if (reverse) {
            for (int32_t i = start-1; i >= 0; i--) {
                if (bitmap[i]) {
                    address pred = trigger-(start-i);
                    result.push_back(pred);
                }
            } 
        } else {
            for (uint32_t i = start+1; i < REGION_SIZE; i++) {
                if (bitmap[i]) {
                    address pred = trigger+(i-start);
                    result.push_back(pred);
                }
            }
        }
        return result;
    }

    virtual bool operator==(const SpatialPattern &other) const override {
        /* casting allowed here because only one type of SpatialPattern
         * can be instantiated in a binary */ 
        Footprint other_map = *((Footprint*) ((void*) &other));
        if (reverse != other_map.reverse)
            return false;
        uint32_t max_start = (start > other_map.start) ? start : other_map.start;
        for (uint32_t i = 0; i < (REGION_SIZE-max_start); i++)
            if (bitmap[i+start] != other_map.bitmap[i+other_map.start])
                return false;
        return true;
    }

    virtual SpatialPattern *clone() const override {
        Footprint *result = new Footprint();
        result->base = base;
        result->start = start;
        result->length = length;
        result->reverse = reverse;
        result->last_addr = last_addr;
        for (uint32_t i = 0; i < REGION_SIZE; i++)
            result->bitmap[i] = bitmap[i];
        return result;
    }

    friend ostream &operator <<(ostream &os, const Footprint &other) {
        for (uint32_t i = 0; i < REGION_SIZE; i++) 
            os << other.bitmap[i];
        return os;
    }
};

#endif
