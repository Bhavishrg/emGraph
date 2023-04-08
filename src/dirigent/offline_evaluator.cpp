#include "offline_evaluator.h"

#include <NTL/BasicThreadPool.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <thread>

#include "helpers.h"

namespace dirigent {
OfflineEvaluator::OfflineEvaluator(int nP, int my_id,
                                   std::shared_ptr<io::NetIOMP> network,
                                   utils::LevelOrderedCircuit circ,
                                   int security_param, int threads, int seed)
    : nP_(nP),
      id_(my_id),
      security_param_(security_param),
      rgen_(my_id, seed),
      network_(std::move(network)),
      circ_(std::move(circ)),
      preproc_(circ.num_gates, circ.outputs.size())
      {

  tpool_ = std::make_shared<ThreadPool>(threads);
}

void OfflineEvaluator::randomShare(int pid, RandGenPool& rgen, io::NetIOMP& network,
                                    AuthAddShare<dirigent::Field>& share, 
                                    TPShare<dirigent::Field>& tpShare) {
  // for all pid = 1 to nP sample common random value
  // pid = 0 stores values in TPShare.values
  // pid = 0 computes secret = secret()
  // pid = 0 computes tag = secret * MAC_key
  // for all pid = 2 to nP sample common random tag
  // pid = 0 stores tags in TPShare.tags
  // pid = 0 sends tag[1] to pid = 1
  dirigent::Field val = 0;
  dirigent::Field tag = 0;
  dirigent::Field tagn = 0;
  
  tpShare.values_.push_back(0);
  tpShare.tags_.push_back(0);
  for(int i = 1; i <= nP_; i++) {
    if(i == pid) {
      rgen.p0().random_data(&val, sizeof(dirigent::Field));
      tpShare.values_.push_back(val);
      share.value_(val);
      if(i != nP_) {
        rgen.p0().random_data(&tag, sizeof(dirigent::Field));
        tpShare.tags_.push_back(tag);
        share.tag_(tag);
      }
    }
  }
    if(pid == 0){
      val = tpShare.secret();
      tag = tpShare.key_ * val;
      tagn = tag;
      for(int i = 1; i < nP; i++){
        tagn -= tpShare.tags_.at(i);
      }
    tpShare.tags_.push_back(tagn);
    network.send(nP_, tagn, tagn.size());
    }
    else if(pid == nP_) {
      network.recv(0, tagn, tagn.size());
      share.tag_(tagn);
    }
}

void OfflineEvaluator::randomShareWithParty(int pid, int dealer, RandGenPool& rgen,
                                            io::NetIOMP& network,
                                            AuthAddShare<dirigent::Field>& share,
                                            TPShare<dirigent::Field>& tpShare, 
                                            dirigent::Field& secret) {
  secret = 0;
  dirigent::Field val = 0;
  dirigent::Field tag = 0;
  dirigent::Field valn = 0;
  dirigent::Field tagn = 0;
  if (pid == 0 || pid == dealer) { 
    rgen.p0().random_data(&secret, sizeof(dirigent::Field));
    tpShare.values_.push_back(secret);
    tpShare.tags_.push_back(secret* tpShare.key_);
  }

  for(int i = 1; i < nP_; i++) {
    if(i == pid) {
      rgen.p0().random_data(&val, sizeof(dirigent::Field));
      tpShare.values_.push_back(val);
      share.value_(val);
      rgen.p0().random_data(&tag, sizeof(dirigent::Field));
      tpShare.tags_.push_back(tag);
      share.tag_(tag);
    }
  }

  val = 0;
  tag = 0;
  if(pid == 0) {
    for(int i = 1; i < nP_; i++) {
      valn += tpShare.values_.at(i) + valn;
      tagn += tpShare.tags_.at(i) + tagn;
    }
    valn = secret - valn;
    tagn = (secret * tpShare.key_) - tagn;
    tpShare.values_.push_back(valn);
    tpShare.tags_.push_back(tagn);
    network.send(nP, valn, valn.size());
    network.send(nP, tagn, tagn.size());
  }
  else if(pid == nP_) {
      network.recv(0, valn, valn.size());
      network.recv(0, tagn, tagn.size());
      share.value_(valn);
      share.tag_(tagn);
  }
}

void OfflineEvaluator::setWireMasks(
    const std::unordered_map<utils::wire_t, int>& input_pid_map) {
  for (const auto& level : circ_.gates_by_level) {
    for (const auto& gate : level) {
      switch (gate->type) {
        case utils::GateType::kInp: {
          auto pregate = std::make_unique<PreprocInput<dirigent::Field>>();

          auto pid = input_pid_map.at(gate->out);
          pregate->pid = pid;
          randomShareWithParty(id_, pid, rgen_, network_, pregate->mask, 
                                 pregate->tpmask, pregate->mask_value);

          preproc_.gates[gate->out] = std::move(pregate);
          break;
        }

        case utils::GateType::kMul: {
          preproc_.gates[gate->out] = std::make_unique<PreprocMultGate<Ring>>();
          randomShare(id_, rgen_, network_,
                      preproc_.gates[gate->out]->mask, 
                      preproc_.gates[gate->out]->tpmask);
          const auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          mult_gates_.push_back(*g);
          break;
        }

        case utils::GateType::kAdd: {
          const auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          const auto& mask_in1 = preproc_.gates[g->in1]->mask;
          const auto& tpmask_in1 = preproc_.gates[g->in1]->tpmask;
          const auto& mask_in2 = preproc_.gates[g->in2]->mask;
          const auto& tpmask_in2 = preproc_.gates[g->in2]->tpmask;
          preproc_.gates[gate->out] =
              std::make_unique<PreprocGate<Ring>>((mask_in1 + mask_in2), (tpmask_in1 + tpmask_in2));
          break;
        }

        case utils::GateType::kSub: {
          const auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          const auto& mask_in1 = preproc_.gates[g->in1]->mask;
          const auto& tpmask_in1 = preproc_.gates[g->in1]->tpmask;
          const auto& mask_in2 = preproc_.gates[g->in2]->mask;
          const auto& tpmask_in2 = preproc_.gates[g->in2]->tpmask;
          preproc_.gates[gate->out] =
              std::make_unique<PreprocGate<Ring>>((mask_in1 - mask_in2),(tpmask_in1 - tpmask_in2));
          break;
        }

        case utils::GateType::kMul: {
          const auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          const auto& mask_in1 = preproc_.gates[g->in1]->mask;
          const auto& tpmask_in1 = preproc_.gates[g->in1]->tpmask;
          const auto& mask_in2 = preproc_.gates[g->in2]->mask;
          const auto& tpmask_in2 = preproc_.gates[g->in2]->tpmask;
          const auto& tp_prod = tpmask_in1.secret() * tpmask_in2.secret();
          randomShare(id_, rgen_, network_,
                      preproc_.gates[gate->out]->mask, 
                      preproc_.gates[gate->out]->tpmask);
          randomShareWithParty(id_, 0, rgen_, network_, 
                                preproc_.gates[gate->out]->mask_prod,
                                preproc_.gates[gate->out]->tpmask_prod, tp_prod);
        }

        default:
          break;
      }
    }
  }
}



PreprocCircuit<dirigent::Field> OfflineEvaluator::getPreproc() {
  return std::move(preproc_);
}

PreprocCircuit<dirigent::Field> OfflineEvaluator::run(
    const std::unordered_map<utils::wire_t, int>& input_pid_map) {
  setWireMasks(input_pid_map);
  return std::move(preproc_);
}


};  // namespace dirigent
