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
                                   quadsquad::utils::LevelOrderedCircuit circ,
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

void OfflineEvaluator::randomShare(int nP, int pid, RandGenPool& rgen, io::NetIOMP& network,
                                    AuthAddShare<Field>& share, 
                                    TPShare<Field>& tpShare) {
  // for all pid = 1 to nP sample common random value
  // pid = 0 stores values in TPShare.values
  // pid = 0 computes secret = secret()
  // pid = 0 computes tag = secret * MAC_key
  // for all pid = 2 to nP sample common random tag
  // pid = 0 stores tags in TPShare.tags
  // pid = 0 sends tag[1] to pid = 1
  Field val = 0;
  Field tag = 0;
  Field tagn = 0;
  
  
  for(int i = 0; i <= nP; i++) {
    if(pid == 0) {
      tpShare.pushValues(0);
      tpShare.pushTags(0);
      share.pushValue(0);
      share.pushTag(0);
    }
    else if(i == pid && i != 0) {
      rgen.p0().random_data(&val, sizeof(Field));
      tpShare.pushValues(val);
      share.pushValue(val);
      if(i != nP) {
        rgen.p0().random_data(&tag, sizeof(Field));
        tpShare.pushTags(tag);
        share.pushTag(tag);
      }
    }
  }
    if(pid == 0){
      val = tpShare.secret();
      tag = tpShare.macKey() * val;
      tagn = 0;
      for(int i = 1; i < nP; i++){
        tagn += tpShare.commonTagWithParty(i);
      }
      
    tpShare.pushTags(tagn);
    network.send(nP, &tagn, sizeof(tagn));
    }
    else if(pid == nP) {
      network.recv(0, &tagn, sizeof(tagn));
      share.pushTag(tagn);
    }
}

void OfflineEvaluator::randomShareWithParty(int pid, int dealer, RandGenPool& rgen,
                                            io::NetIOMP& network,
                                            AuthAddShare<Field>& share,
                                            TPShare<Field>& tpShare, 
                                            Field& secret) {
  secret = 0;
  Field val = 0;
  Field tag = 0;
  Field valn = 0;
  Field tagn = 0;
  
  if (pid == 0 || pid == dealer) { 
    rgen.p0().random_data(&secret, sizeof(Field));
    tpShare.pushValues(secret);
    tpShare.pushTags(secret* tpShare.macKey());
  }

  for(int i = 1; i < party_num(); i++) {
    if(i == pid) {
      rgen.p0().random_data(&val, sizeof(Field));
      tpShare.pushValues(val);
      share.pushValue(val);
      rgen.p0().random_data(&tag, sizeof(Field));
      tpShare.pushTags(tag);
      share.pushTag(tag);
    }
  }

  val = 0;
  tag = 0;
  if(pid == 0) {
    for(int i = 1; i < nP_; i++) {
      valn += tpShare.commonValueWithParty(i) + valn;
      tagn += tpShare.commonTagWithParty(i) + tagn;
    }
    valn = secret - valn;
    tagn = (secret * tpShare.macKey()) - tagn;
    tpShare.pushValues(valn);
    tpShare.pushTags(tagn);
    network.send(nP_, &valn, sizeof(valn));
    network.send(nP_, &tagn, sizeof(tagn));
  }
  else if(pid == nP_) {
      network.recv(0, &valn, sizeof(valn));
      network.recv(0, &tagn, sizeof(tagn));
      share.pushValue(valn);
      share.pushTag(tagn);
  }
}

void OfflineEvaluator::setWireMasks(
    const std::unordered_map<quadsquad::utils::wire_t, int>& input_pid_map) {
  for (const auto& level : circ_.gates_by_level) {
    for (const auto& gate : level) {
      switch (gate->type) {
        case quadsquad::utils::GateType::kInp: {
          auto pregate = std::make_unique<PreprocInput<Field>>();

          auto pid = input_pid_map.at(gate->out);
          pregate->pid = pid;
          randomShareWithParty(id_, pid, rgen_, *network_, pregate->mask, 
                                 pregate->tpmask, pregate->mask_value);

          preproc_.gates[gate->out] = std::move(pregate);
          break;
        }

        case quadsquad::utils::GateType::kAdd: {
          const auto* g = static_cast<quadsquad::utils::FIn2Gate*>(gate.get());
          const auto& mask_in1 = preproc_.gates[g->in1]->mask;
          const auto& tpmask_in1 = preproc_.gates[g->in1]->tpmask;
          const auto& mask_in2 = preproc_.gates[g->in2]->mask;
          const auto& tpmask_in2 = preproc_.gates[g->in2]->tpmask;
          preproc_.gates[gate->out] =
              std::make_unique<PreprocGate<Field>>((mask_in1 + mask_in2), (tpmask_in1 + tpmask_in2));
          break;
        }

        case quadsquad::utils::GateType::kSub: {
          const auto* g = static_cast<quadsquad::utils::FIn2Gate*>(gate.get());
          const auto& mask_in1 = preproc_.gates[g->in1]->mask;
          const auto& tpmask_in1 = preproc_.gates[g->in1]->tpmask;
          const auto& mask_in2 = preproc_.gates[g->in2]->mask;
          const auto& tpmask_in2 = preproc_.gates[g->in2]->tpmask;
          preproc_.gates[gate->out] =
              std::make_unique<PreprocGate<Field>>((mask_in1 - mask_in2),(tpmask_in1 - tpmask_in2));
              //std::move(preproc_.gates[gate->out]);

          break;
        }

        case quadsquad::utils::GateType::kMul: {
          preproc_.gates[gate->out] = std::make_unique<PreprocMultGate<Field>>();
          const auto* g = static_cast<quadsquad::utils::FIn2Gate*>(gate.get());
          const auto& mask_in1 = preproc_.gates[g->in1]->mask;
          const auto& tpmask_in1 = preproc_.gates[g->in1]->tpmask;
          const auto& mask_in2 = preproc_.gates[g->in2]->mask;
          const auto& tpmask_in2 = preproc_.gates[g->in2]->tpmask;
          auto tp_prod = tpmask_in1.secret() * tpmask_in2.secret();
          TPShare<Field> tprand_mask;
          AuthAddShare<Field> rand_mask;
          randomShare(nP_, id_, rgen_, *network_, rand_mask, tprand_mask);
                    
          TPShare<Field> tpmask_product;
          AuthAddShare<Field> mask_product; 
          randomShareWithParty(id_, 0, rgen_, *network_, 
                                mask_product, tpmask_product, tp_prod);
          preproc_.gates[gate->out] = std::move(std::make_unique<PreprocMultGate<Field>>
                              (rand_mask, tprand_mask, mask_product, tpmask_product));
        }

        default:
          break;
      }
    }
  }
}



PreprocCircuit<Field> OfflineEvaluator::getPreproc() {
  return std::move(preproc_);
}

PreprocCircuit<Field> OfflineEvaluator::run(
    const std::unordered_map<quadsquad::utils::wire_t, int>& input_pid_map) {
  setWireMasks(input_pid_map);
  return std::move(preproc_);
}


};  // namespace dirigent
