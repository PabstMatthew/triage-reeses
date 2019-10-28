//Hawkeye Cache Replacement Tool v2.0
//UT AUSTIN RESEARCH LICENSE (SOURCE CODE)
//The University of Texas at Austin has developed certain software and documentation that it desires to
//make available without charge to anyone for academic, research, experimental or personal use.
//This license is designed to guarantee freedom to use the software for these purposes. If you wish to
//distribute or make other use of the software, you may purchase a license to do so from the University of
//Texas.
///////////////////////////////////////////////
//                                            //
//     Hawkeye [Jain and Lin, ISCA' 16]       //
//     Akanksha Jain, akanksha@cs.utexas.edu  //
//                                            //
///////////////////////////////////////////////

#ifndef PREDICTOR_H
#define PREDICTOR_H

using namespace std;

#include <iostream>

#include <math.h>
#include <set>
#include <vector>
#include <map>
#include <set>
#include <algorithm>

uint64_t CRC( uint64_t _blockAddress )
{
    static const unsigned long long crcPolynomial = 3988292384ULL;
    unsigned long long _returnVal = _blockAddress;
    for( unsigned int i = 0; i < 32; i++ )
        _returnVal = ( ( _returnVal & 1 ) == 1 ) ? ( ( _returnVal >> 1 ) ^ crcPolynomial ) : ( _returnVal >> 1 );
    return _returnVal;
}


class HAWKEYE_PC_PREDICTOR
{
    map<uint64_t, short unsigned int > SHCT;

       public:

    void increment (uint64_t pc)
    {
        uint64_t signature = CRC(pc) % SHCT_SIZE;
        if(SHCT.find(signature) == SHCT.end())
            SHCT[signature] = (1+MAX_SHCT)/2;

        SHCT[signature] = (SHCT[signature] < MAX_SHCT) ? (SHCT[signature]+1) : MAX_SHCT;

    }

    void saturate (uint64_t pc)
    {
        uint64_t signature = CRC(pc) % SHCT_SIZE;
        assert(SHCT.find(signature) != SHCT.end());

        SHCT[signature] = MAX_SHCT;

    }


    void decrement (uint64_t pc)
    {
        uint64_t signature = CRC(pc) % SHCT_SIZE;
        if(SHCT.find(signature) == SHCT.end())
            SHCT[signature] = (1+MAX_SHCT)/2;
        if(SHCT[signature] != 0)
            SHCT[signature] = SHCT[signature]-1;
    }

    bool get_prediction (uint64_t pc)
    {
        uint64_t signature = CRC(pc) % SHCT_SIZE;
        if(SHCT.find(signature) != SHCT.end() && SHCT[signature] < ((MAX_SHCT+1)/2))
            return false;
        return true;
    }
};

class HAWKEYE_IDEALPC_PREDICTOR
{
    map<uint64_t, uint32_t> detraining_count;
    map<uint64_t, uint32_t> training_count;
    map<uint64_t, uint32_t> prediction_count;
    map<uint64_t, uint32_t> positive_count;
    map<uint64_t, uint32_t> negative_count;

    public:
    void increment (uint64_t pc)
    {
        if(training_count.find(pc) == training_count.end())
        {
            detraining_count[pc] = 0;
            training_count[pc] = 0;
            positive_count[pc] = 0;
            negative_count[pc] = 0;
        }

        training_count[pc]++;
        positive_count[pc]++;
    }

    void decrement (uint64_t pc)
    {
        if(training_count.find(pc) == training_count.end())
        {
            detraining_count[pc] = 0;
            training_count[pc] = 0;
            prediction_count[pc] = 0;
            positive_count[pc] = 0;
            negative_count[pc] = 0;
        }

        training_count[pc]++;
        negative_count[pc]++;
    }

    void detrain (uint64_t pc)
    {
        if(training_count.find(pc) == training_count.end())
        {
            detraining_count[pc] = 0;
            training_count[pc] = 0;
            prediction_count[pc] = 0;
            positive_count[pc] = 0;
            negative_count[pc] = 0;
        }

        detraining_count[pc]++;
    }


    bool get_prediction (uint64_t pc)
    {
        if(training_count.find(pc) == training_count.end())
            return true;

        if(training_count[pc] == 0)
            return true;
        //cout << "Prediction: "<< hex << PC << " " << training_count[PC] << " " << (positive_count[PC] >= negative_count[PC]) << endl;
        //if(positive_count[pc] >= negative_count[pc])
        if(positive_count[pc] >= (detraining_count[pc] + negative_count[pc]))
            return true;
        return false;
    }

    double get_probability (uint64_t pc)
    {
        if(training_count.find(pc) == training_count.end())
            return 2; 

        if(training_count[pc] == 0)
            return 2;
        //cout << "Prediction: "<< hex << PC << " " << training_count[PC] << " " << (positive_count[PC] >= negative_count[PC]) << endl;
        return ((double)positive_count[pc]/(double)(training_count[pc]));

    }

    double get_detrain_probability (uint64_t pc)
    {
        if(training_count.find(pc) == training_count.end())
            return 2; 

        if(training_count[pc] == 0)
            return 2;
        //cout << "Prediction: "<< hex << PC << " " << training_count[PC] << " " << (positive_count[PC] >= negative_count[PC]) << endl;
        return ((double)positive_count[pc]/(double)(training_count[pc] + detraining_count[pc]));

    }

};

class HAWKEYE_PAIRPC_PREDICTOR
{
    HAWKEYE_IDEALPC_PREDICTOR* baseline_hawkeye_predictor;
    map<uint64_t, map<pair<uint64_t, uint64_t>, uint32_t> > training_count;
    map<uint64_t, map<pair<uint64_t, uint64_t>, uint32_t> > positive_count;
    map<uint64_t, map<pair<uint64_t, uint64_t>, uint32_t> > negative_count;

    public:
    HAWKEYE_PAIRPC_PREDICTOR()
    {
        baseline_hawkeye_predictor = new HAWKEYE_IDEALPC_PREDICTOR();
    }

    void increment (uint64_t pc, vector<uint64_t> pc_history)
    {
        baseline_hawkeye_predictor->increment(pc);
        if(pc_history.size() == 0)
            return;

        if(training_count.find(pc) == training_count.end())
        {
            training_count[pc].clear();
            positive_count[pc].clear();
            negative_count[pc].clear();
        }

        sort(pc_history.begin(), pc_history.end());

        set<pair<uint64_t, uint64_t> > pair_list;
        for(unsigned int i=0; i<pc_history.size()-1; i++)
        {
            for(unsigned int j=i+1; j<pc_history.size(); j++)
            {
                pair<uint64_t, uint64_t> new_pair = make_pair(pc_history[i], pc_history[j]);
                if(pair_list.find(new_pair) == pair_list.end())
                    pair_list.insert(new_pair);
            }
        }

//        cout << hex << pc << " " << dec << training_count[pc].size() << endl;
        for(set<pair<uint64_t, uint64_t> >::iterator it=pair_list.begin(); it != pair_list.end(); it++)
        {
         //   cout << hex << (*it).first << " " << (*it).second << dec << endl;
            if(training_count[pc].find(*it) == training_count[pc].end())
            {
                training_count[pc][*it] = 0;
                positive_count[pc][*it] = 0;
                negative_count[pc][*it] = 0;
            }

            training_count[pc][*it] += 1;
            positive_count[pc][*it] += 1;
            assert(training_count[pc][*it] != 0);
        }
    }

    void decrement (uint64_t pc, vector<uint64_t> pc_history)
    {
        baseline_hawkeye_predictor->decrement(pc);
        if(pc_history.size() == 0)
            return;
        if(training_count.find(pc) == training_count.end())
        {
            training_count[pc].clear();
            positive_count[pc].clear();
            negative_count[pc].clear();
        }

        sort(pc_history.begin(), pc_history.end());

        set<pair<uint64_t, uint64_t> > pair_list;
        for(unsigned int i=0; i<pc_history.size()-1; i++)
        {
            for(unsigned int j=i+1; j<pc_history.size(); j++)
            {
                pair<uint64_t, uint64_t> new_pair = make_pair(pc_history[i], pc_history[j]);
                if(pair_list.find(new_pair) == pair_list.end())
                    pair_list.insert(new_pair);
            }
        }

//        cout << "Decrement " << hex << pc << dec << endl;
        for(set<pair<uint64_t, uint64_t> >::iterator it=pair_list.begin(); it != pair_list.end(); it++)
        {
 //           cout << hex << (*it).first << " " << (*it).second << dec << endl;
            if(training_count[pc].find(*it) == training_count[pc].end())
            {
                training_count[pc][*it] = 0;
                positive_count[pc][*it] = 0;
                negative_count[pc][*it] = 0;
            }

            training_count[pc][*it] += 1;
            negative_count[pc][*it] += 1;

            assert(training_count[pc][*it] != 0);
        }

    }

    bool get_prediction (uint64_t pc, vector<uint64_t> pc_history)
    {
        bool baseline_prediction = baseline_hawkeye_predictor->get_prediction(pc);
        if(pc_history.size() <= 1)
            return baseline_prediction;

        if(training_count.find(pc) == training_count.end())
            return true; 

        sort(pc_history.begin(), pc_history.end());

        set<pair<uint64_t, uint64_t> > pair_list;
        for(unsigned int i=0; i<pc_history.size()-1; i++)
        {
            for(unsigned int j=i+1; j<pc_history.size(); j++)
            {
                pair<uint64_t, uint64_t> new_pair = make_pair(pc_history[i], pc_history[j]);
                if(pair_list.find(new_pair) == pair_list.end())
                    pair_list.insert(new_pair);
            }
        }

        //cout << "Predict " << endl;
        uint32_t train_count =0, pos_count =0;
        for(set<pair<uint64_t, uint64_t> >::iterator it=pair_list.begin(); it != pair_list.end(); it++)
        {
            if(training_count[pc].find(*it) != training_count[pc].end())
            {
                //cout << "Found " << hex << (*it).first << " " << (*it).second << dec << " " << training_count[pc][*it] << endl;
                assert(training_count[pc][*it] != 0);
                train_count += training_count[pc][*it];
                pos_count += positive_count[pc][*it];
            }
        }

//        cout << "Predict: " << hex << pc << " " << dec << pos_count << " " << train_count << endl;
        //TODO: We should fall back to something simpler here
        if(train_count == 0)
            return true;

        assert(pos_count <= train_count);
        if(pos_count >= (train_count - pos_count))
            return true;
        return false;
    }

    double get_probability (uint64_t pc, vector<uint64_t> pc_history)
    {
        return 0.0;
    }

};

#endif
