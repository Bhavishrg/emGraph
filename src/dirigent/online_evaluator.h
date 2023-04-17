#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "../io/netmp.h"
#include "../utils/circuit.h"
#include "preproc.h"
#include "rand_gen_pool.h"
#include "sharing.h"
#include "types.h"

namespace dirigent{
class OnlineEvaluator {
    int nP_;
    int id_;
    int security_param_;
    RandGenPool rgen_;
    std::shared_ptr<io::NetIOMP> network_;
    PreprocCircuit<Field> preproc_;
    quadsquad::utils::LevelOrderedCircuit circ_;
    std::vector<Field> wires_;
    std::vector<Field> q_val_;
    std::vector<AuthAddShare<Field>> q_sh_;
    std::shared_ptr<ThreadPool> tpool_;

    // write reconstruction function
     public:
        OnlineEvaluator(int nP, int id, std::shared_ptr<io::NetIOMP> network,
                        PreprocCircuit<Field> preproc, 
                        quadsquad::utils::LevelOrderedCircuit circ,
                        int security_param, int threads, int seed = 200);
                        
        OnlineEvaluator(int nP, int id, std::shared_ptr<io::NetIOMP> network,
                        PreprocCircuit<Field> preproc, 
                        quadsquad::utils::LevelOrderedCircuit circ,
                        int security_param, 
                        std::shared_ptr<ThreadPool> tpool, int seed = 200);

        void setInputs(const std::unordered_map<quadsquad::utils::wire_t, Field>& inputs);

        void setRandomInputs();

        void evaluateGatesAtDepth(size_t depth);

        bool MACVerification();

        std::vector<Field> getOutputs();

        // Reconstruct an authenticated additive shared value
        // combining multiple values might be more effficient
        // CHECK
        Field reconstruct(AuthAddShare<Field>& shares);

        // Evaluate online phase for circuit
        std::vector<Field> evaluateCircuit(
            const std::unordered_map<quadsquad::utils::wire_t, Field>& inputs);
        

    };
}; //namespace dirigent