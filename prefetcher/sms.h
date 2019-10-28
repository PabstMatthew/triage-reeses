#include <list>
#include <vector>
#include <map>
#include <string>
#include <iostream>

#include "cache.h"
#include "container.h"

#define DEBUG false
#define DBCODE(x) do { if(DEBUG) {x} } while(0);

namespace SMS {
    #define AGT_HEIGHT 4
    #define AGT_WIDTH 16

    #define PHT_HEIGHT 256
    #define PHT_WIDTH 8

    #define DEGREE 1
    
    const uint64_t REGION_SIZE  = 4096; // In bytes
    const uint64_t REGION_SHIFT = lg2(REGION_SIZE);
    const uint64_t REGION_MASK  = (1ULL << REGION_SHIFT) - 1;
    const uint64_t NUM_BLOCKS   = REGION_SIZE / BLOCK_SIZE; // number of blocks per region

    // Return the region offset of an address
    int64_t getOffset(uint64_t address) { 
        return (address & REGION_MASK) >> lg2(BLOCK_SIZE);
    } 

    // Return the region tag of an address
    uint64_t getRegionTag(uint64_t address) {
        return address & ~REGION_MASK;
    }

    struct AGT_entry {
        uint64_t pc;
        int64_t offset; 
        uint64_t pattern;
        
        AGT_entry(uint64_t aPc = 0, int64_t aOffset = 0, uint64_t aPattern = 0) :pc{aPc},  offset{aOffset}, pattern{aPattern} {}   
    };

    struct PHT_entry {
        uint64_t pc;
        uint64_t pattern; 

        PHT_entry(uint64_t aPc = 0, uint64_t aPattern = 0) :pc{aPc}, pattern{aPattern} {}
    };

    typedef struct AGT_entry AGT_entry; 
    typedef struct PHT_entry PHT_entry; 

    class SMS {

    private: 
        typedef Container<uint64_t, AGT_entry> AGT; AGT theAGT;
        typedef Container<uint64_t, PHT_entry> PHT; PHT thePHT;
    public: 
        typedef uint64_t pattern_t;
        typedef uint64_t address_t;

        typedef std::map<string, uint64_t> StatCounter;
        StatCounter statCounter; 

        #define STAT(n) do { ++statCounter[(#n)]; } while(0);

        SMS():
            theAGT(AGT_HEIGHT, AGT_WIDTH, REGION_SHIFT, 27-REGION_SHIFT),
            thePHT(PHT_HEIGHT, PHT_WIDTH, 0, 14)
        {}

        bool prefetchAddresses(address_t address, address_t ip,
                vector<address_t>& pf_addrs) {
            PHT::Iter pht_ent = thePHT.find(ip);
            pf_addrs.clear();
            if(pht_ent != thePHT.end()){
                pattern_t pattern = pht_ent->second.pattern;
                address_t region_base(getRegionTag(address));

                uint64_t mask = 1ULL;
                for(unsigned int i = 0; i < NUM_BLOCKS; ++i) {
                    if(pattern & mask) {
                        pf_addrs.push_back(region_base + (i << LOG2_BLOCK_SIZE));
                        STAT(PREFETCHES_COMPUTED);
                    }
                    mask = mask << 1;
                }

                return true; 
            // convert the given pattern to a vector of addresses
            } 

            return false;

            // get the spatial pattern and compute the addressed to 
            // be prefetched. 
        }
 
        bool observePattern(address_t address, address_t ip){
            // check if line belongs in a generation in the table 
            // allocate a new entry otherwise
            // Note if the AGT is full move the evicted entry to the PHT 
            int64_t a_region_tag (getRegionTag(address));
            int64_t a_offset (getOffset(address)); // why do we have to store this?
            AGT::Iter agt_entry = theAGT.find(a_region_tag);

            AGT::Item agt_evicted;
            bool newgen = false;
            if(agt_entry == theAGT.end()) {
                pattern_t new_pattern = updatePattern(0, a_offset);
                //std::cout << "AGT " << address << " " << ip << std::endl;
                agt_evicted = theAGT.insert(a_region_tag, AGT_entry(ip, a_offset, new_pattern));
                DBCODE(std::cout << "(1)generation started: " << getRegionTag(address)  <<  std::endl;)
                STAT(NEW_AGT_ENTRIES)
                newgen = true;
            } else {
                pattern_t pat = agt_entry->second.pattern; 
                pat = updatePattern(pat, a_offset);
                agt_entry->second.pattern = pat;
                STAT(AGT_UPDATES)
                DBCODE(std::cout << "(1)generation updated: " << getRegionTag(address) << " pattern: " << agt_entry->second.pattern << std::endl;)
            }

            if(agt_evicted.second.pattern) {
                STAT(AGT_EVICTIONS)
                if(thePHT.erase(agt_evicted.second.pc)) { 
                    STAT(PHT_ERASURES)
                    DBCODE(std::cout << "(1)erased previous PHT entry:  " << agt_evicted.second.pc << std::endl;)
                }
                if(agt_evicted.second.pattern & (agt_evicted.second.pattern - 1)) {
                    STAT(PHT_ENTRIES)
                    thePHT.insert(agt_evicted.second.pc, PHT_entry(agt_evicted.second.pc, agt_evicted.second.pattern));
                }
            }

            return newgen;
        }

        pattern_t updatePattern(pattern_t current, uint64_t offset) {
            // TODO: make the patterns rotated
            // Not rotated
            current |= 1UL << offset;
            return current; 
        }        

        void endGeneration(address_t address) {
            // this method should be called when a line is evicted
            // this will check if the the address is a part of an active generation 
            // and move it to a PHT
            address_t region_tag = getRegionTag(address);
            int32_t region_offset = getOffset(address);
            DBCODE(std::cout << "EVICTED: " << region_tag << std::endl;)
            AGT::Item agt_evicted;

            AGT::Iter agt_ent = theAGT.find(region_tag);
            if(agt_ent != theAGT.end()) {
               
                pattern_t new_bit = updatePattern(0, region_offset);
                if(agt_ent->second.pattern & new_bit){
                    agt_evicted = *agt_ent;
                    theAGT.erase(region_tag);
                }
            }

            pattern_t pat = agt_evicted.second.pattern;
            address_t pc = agt_evicted.second.pc;
            PHT_entry entry(pc, pat);

            if((pat & (pat - 1)) != 0) { 
                if(thePHT.erase(pc)){
                    STAT(PHT_ERASURES)
                    DBCODE(std::cout << "erase previous PHT entry: " << pc << std::endl;)
                }
                STAT(PHT_ENTRIES)
                DBCODE(std::cout << "generation ended: " << agt_evicted.first << " pattern: " << pat << std::endl;)
                PHT::Item pht_evicted = thePHT.insert(pc, entry);
                
                if(pht_evicted.second.pattern) {
                    STAT(PHT_EVICTIONS)
                }
            }
            // move only if pattern != 2^k
        }

        std::queue<uint64_t> buffer;
    };
} // SMS

static SMS::SMS *sms;

uint64_t total_prefetches;
uint64_t total_prefetch_triggers;

void sms_l2c_prefetcher_initialize() {
    sms = new SMS::SMS();
    total_prefetches = 0;
    total_prefetch_triggers = 0;
}

// is called when a request is made to the next level memory
void sms_l2c_prefetcher_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, CACHE* cache) {
    for (int i = 0; i < DEGREE && !sms->buffer.empty(); i++) {
        DBCODE(cout << "PREFETCHING: " << sms->buffer.front() << " pc: " << ip << " sim_cycle: " << current_core_cycle[0] << endl;)
        cache->prefetch_line(ip, addr, sms->buffer.front(), FILL_LLC, 0);
        sms->buffer.pop();

    }

    if(sms->observePattern(addr, ip)) {
        vector<uint64_t> pf_addrs;
        sms->prefetchAddresses(addr, ip, pf_addrs);

        DBCODE(if(pf_addrs.size()) {
           cout << "PREFETCHED: " << pf_addrs.size() << " pc: " << ip << endl;
        })

        total_prefetch_triggers++;
        for(size_t i = 0; i < pf_addrs.size(); ++i) {
            sms->buffer.push(pf_addrs[i]);
            total_prefetches++;
        }
    }
}

// when a requested line is filled (may require a line from the cache to be evicted)
void sms_l2c_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, CACHE* cache) {
    sms->endGeneration(evicted_addr);
}

void sms_l2c_prefetcher_final_stats() {
    cout << "SMS prefetcher final stats" << endl;
    for(auto i = sms->statCounter.begin(); i != sms->statCounter.end(); ++i){
        cout << i->first << " " << i->second << endl;
    }
    cout << "total prefetches: " << total_prefetches << endl;
    cout << "total prefetch triggers: " << total_prefetch_triggers << endl;
    cout << "avg degree: " << (total_prefetches*1.0/total_prefetch_triggers) << endl;
}

