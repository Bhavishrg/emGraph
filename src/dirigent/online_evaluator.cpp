#include"online_evaluator.h"

#include <array>

#include"dirigent/helpers.h"

namespace dirigent {
OnlineEvaluator::OnlineEvaluator(int id, std::shared_ptr<io::NetIOMP> network,
                                PreprocCircuit<dirigent::Field> preproc,
                                utils::LevelOrderedCircuit circ,
                                int security_param, int threads, int seed)
    : id_(id),
      security_param_(security_param),
      rgen_(id,seed),
      neetwork_(std::move(network)),
      preproc_(preproc),
      circ_(std::move(circ)),
      wires_(circ.num_gates) {tpool_ = std::make_shared<ThreadPool>(threads); }

OnlineEvaluator::OnlineEvaluator(int id, std::shared_ptr<io::NetIOMP> network,
                                PreprocCircuit<dirigent::Field> preproc,
                                utils::LevelOrderedCircuit circ,
                                int security_param,
                                std::shared_ptr<ThreadPool> tpool, int seed)
    : id_(id),
      security_param_(security_param),
      rgen_(id, seed),
      network_(std::move(network)),
      preproc_(std::move(preproc)),
      circ_(std::move(circ)),
      tpool_(std::move(tpool)),
      wires_(circ.num_gates) {}

void OnlineEvaluator::setInputs(const std::unordered_map<utils::wire_t, dirigent::Field>& inputs) {

}

void OnlineEvaluator::setRandomInputs() {

}

void OnlineEvaluator::evaluateGatesAtDepth(size_t depth) {

}

std::vector<dirigent::Field> OnlineEvaluator::getOutputs() {

}

dirigent::Field OnlineEvaluator::reconstruct(AuthAddShare<dirigent::Field>& shares) {

}

std::vector<dirigent::Field> OnlineEvaluator::evaluateCircuit( const std::unordered_map<utils::wires_t, dirigent::Field>& inputs) {
    
}
      
}; //namespace dirigent