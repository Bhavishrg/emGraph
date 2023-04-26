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
    quadsquad::utils::LevelOrderedCircuit multk_circ_;
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
        
        void eqzEvaluate(const std::vector<quadsquad::utils::FIn1Gate>& eqz_gates);

        void evaluateGatesAtDepthPartySend(size_t depth, 
                                std::vector<Field>& mult_nonTP, std::vector<Field>& r_mult_pad,
                                std::vector<Field>& mult3_nonTP, std::vector<Field>& r_mult3_pad,
                                std::vector<Field>& mult4_nonTP, std::vector<Field>& r_mult4_pad,
                                std::vector<Field>& dotprod_nonTP, std::vector<Field>& r_dotprod_pad);

        void evaluateGatesAtDepthPartyRecv(size_t depth, 
                                    std::vector<Field> mult_all, std::vector<Field> r_mult_pad,
                                    std::vector<Field> mult3_all, std::vector<Field> r_mult3_pad,
                                    std::vector<Field> mult4_all, std::vector<Field> r_mult4_pad,
                                    std::vector<Field> dotprod_all, std::vector<Field> r_dotprod_pad);

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


class BoolEvaluator {
  int nP_;
  int id_;
  RandGenPool rgen_;
  std::shared_ptr<io::NetIOMP> network_;
  PreprocCircuit<BoolRing> preproc_;
  quadsquad::utils::LevelOrderedCircuit circ_;
  std::vector<BoolRing> wires_;
  std::vector<BoolRing> q_val_;
  std::vector<AuthAddShare<BoolRing>> q_sh_;
//   std::vector<BoolRing> vwires;
//   preprocg_ptr_t<BoolRing>* vpreproc;
//   quadsquad::utils::LevelOrderedCircuit circ;

public:
  BoolEvaluator(int nP, int id, std::shared_ptr<io::NetIOMP> network, 
                PreprocCircuit<BoolRing> preproc, 
                quadsquad::utils::LevelOrderedCircuit circ,
                int seed = 200);
  
  void setInputs(const std::unordered_map<quadsquad::utils::wire_t, BoolRing>& inputs);

  void setRandomInputs();

//   static std::vector<BoolRing> reconstruct(
//       int id, const std::array<std::vector<BoolRing>, 3>& recon_shares,
//       io::NetIOMP& network, JumpProvider& jump, ThreadPool& tpool);
  void evaluateGatesAtDepthPartySend(size_t depth,  
                                std::vector<BoolRing>& mult_nonTP, std::vector<BoolRing>& r_mult_pad,
                                std::vector<BoolRing>& mult3_nonTP, std::vector<BoolRing>& r_mult3_pad,
                                std::vector<BoolRing>& mult4_nonTP, std::vector<BoolRing>& r_mult4_pad,
                                std::vector<BoolRing>& dotprod_nonTP, std::vector<BoolRing>& r_dotprod_pad);
  void evaluateGatesAtDepthPartyRecv(size_t depth, 
                                std::vector<BoolRing> mult_all, std::vector<BoolRing> r_mult_pad,
                                std::vector<BoolRing> mult3_all, std::vector<BoolRing> r_mult3_pad,
                                std::vector<BoolRing> mult4_all, std::vector<BoolRing> r_mult4_pad,
                                std::vector<BoolRing> dotprod_all, std::vector<BoolRing> r_dotprod_pad);
  void evaluateGatesAtDepth(size_t depth);
  void evaluateAllLevels();

//   std::vector<std::vector<BoolRing>> getOutputShares();
};
}; //namespace dirigent