#ifndef ISB_HAWKEYE_PREDICTOR_H
#define ISB_HAWKEYE_PREDICTOR_H

#include <iostream>

#include <math.h>
#include <set>
#include <vector>
#include <map>
#include <set>
#include <algorithm>

#define MAX_SHCT 8191
#define SHCT_SIZE_BITS 14
#define SHCT_SIZE (1<<SHCT_SIZE_BITS)

inline uint64_t MyCRC( uint64_t _blockAddress)
{
    static const unsigned long long crcPolynomial = 3988292384ULL;
    unsigned long long _returnVal = _blockAddress;
    for( unsigned int i = 0; i < 32; i++ )
        _returnVal = ( ( _returnVal & 1 ) == 1 ) ? ( ( _returnVal >> 1 ) ^ crcPolynomial ) : ( _returnVal >> 1 );
    return _returnVal;
}


class IsbHawkeyePCPredictor
{
    map<uint64_t, short unsigned int > SHCT;

    public:
    void increment(uint64_t pc)
    {
        uint64_t signature = MyCRC(pc) % SHCT_SIZE;
        if(SHCT.find(signature) == SHCT.end())
            SHCT[signature] = (1+MAX_SHCT)/2;

        SHCT[signature] = (SHCT[signature] < MAX_SHCT) ? (SHCT[signature]+1) : MAX_SHCT;

    }

    void saturate(uint64_t pc)
    {
        uint64_t signature = MyCRC(pc) % SHCT_SIZE;
        assert(SHCT.find(signature) != SHCT.end());

        SHCT[signature] = MAX_SHCT;

    }

    void decrement (uint64_t pc)
    {
        uint64_t signature = MyCRC(pc) % SHCT_SIZE;
        if(SHCT.find(signature) == SHCT.end())
            SHCT[signature] = (1+MAX_SHCT)/2;
        if(SHCT[signature] != 0)
            SHCT[signature] = SHCT[signature]-1;
    }

    bool get_prediction (uint64_t pc)
    {
        uint64_t signature = MyCRC(pc) % SHCT_SIZE;
        if(SHCT.find(signature) != SHCT.end() && SHCT[signature] < ((MAX_SHCT+1)/2))
            return false;
        return true;
    }
};

#endif
