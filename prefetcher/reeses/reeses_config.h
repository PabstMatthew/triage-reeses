#ifndef REESES_CONFIG_H
#define REESES_CONFIG_H

#include <cstdint>

// debugging macro
//#define DEBUG
#ifdef DEBUG
#define D(x) x
#else
#define D(x)
#endif

namespace reeses {

/* size of regions in cache blocks
 *  determines footprint size
 *  determines maximum delta pattern length
 *  (default 64 -> 4KB) */
const uint32_t REGION_SIZE = 64;
const uint32_t LOG2_REGION_SIZE = 6;

/* determines size and default value of confidence counters
 *  MAX_CONF should probably be 2^x-1
 *  (default 2 and 3) */
const uint32_t INIT_CONF = 2;
const uint32_t MAX_CONF = 3;

// experimental feature
const uint32_t SPATIAL_MAX = 15;
const uint32_t SPATIAL_INC = 1;
const uint32_t SPATIAL_DEC = 1;

/* number of prefetches ahead of program 
 *  (default 0) */ 
const uint32_t LOOKAHEAD = 8;

/* max number of metadata entries to prefetch ahead
 *  (default ) */
const uint32_t METADATA_DEGREE = 4;

/* size of the SP and PS caches 
 *  (default 512 ways and 8 sets for 4096 entries) */
const uint32_t AMC_SETS = 512;
const uint32_t AMC_WAYS = 8;

/* size of the offset cache
 *  (default 64 ways, 16 sets ) */
const uint32_t OC_SETS = REGION_SIZE;
const uint32_t OC_WAYS = 16;

const uint32_t MAX_STREAM_LENGTH = 256;
const uint32_t LOG2_MAX_STREAM_LENGTH = 8;

const uint32_t INVALID_STR_ADDR = 0xffffffff;

/* turns on unlimited storage for the AMCs */
const bool IDEAL_AMC = false;

/* turns on unlimited storage for the offset cache */
const bool IDEAL_OC = true;

/* turns on ideal off-chip metadata tracking to avoid redundant traffic */
const bool IDEAL_TRAFFIC = true;

/* turns off spatial pattern creation */
const bool NO_SPATIAL = false;

const bool NO_COMPULSORY_PF = false;
const bool NO_SPATIAL_PF = false;
const bool NO_TEMPORAL_PF = false;

}

#endif
