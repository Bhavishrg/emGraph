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
  // for all pid = 1 to nP-1 sample common random tag
  // pid = 0 stores tags in TPShare.tags
  // pid = 0 sends tag[1] to pid = nP
  
  Field val = 0;
  Field tag = 0;
  Field tagn = 0;
  
  
    if(pid == 0) {
      share.pushValue(0);
      share.pushTag(0);
      tpShare.pushValues(0);
      tpShare.pushTags(0);
      for(int i = 1; i <= nP; i++) {
        rgen.pi(i).random_data(&val, sizeof(Field));
        tpShare.pushValues(val);
  
        rgen.pi(i).random_data(&tag, sizeof(Field));
        if( i != nP) {
          tpShare.pushTags(tag);
          tagn += tag;
          
        }
      }
      tagn = tpShare.macKey() * tpShare.secret() - tagn;
      tpShare.pushTags(tagn);
      network.send(nP, &tagn, sizeof(tagn));
    }
    else if(pid > 0) {
      rgen.p0().random_data(&val, sizeof(Field));
      share.pushValue(val);
      
      rgen.p0().random_data(&tag, sizeof(Field));
      
      if( pid != nP) {
        share.pushTag(tag);
      }
      else if(pid == nP) {
        network.recv(0, &tagn, sizeof(tagn)); 
        share.pushTag(tagn);
      }
    }

}

void OfflineEvaluator::randomShareWithParty(int nP, int pid, int dealer, RandGenPool& rgen,
                                            io::NetIOMP& network,
                                            AuthAddShare<Field>& share,
                                            TPShare<Field>& tpShare, 
                                            Field& secret) {
  Field tagF = 0;
  Field val = 0;
  Field tag = 0;
  Field valn = 0;
  Field tagn = 0;
  
  if( pid == 0) {
    if(dealer != 0) {
      rgen.pi(dealer).random_data(&secret, sizeof(Field));
    }
    else {
      rgen.self().random_data(&secret, sizeof(Field));
    }
    
    share.pushValue(0);
    share.pushTag(0);
    tpShare.pushValues(0);
    tpShare.pushTags(0);
    tagF = tpShare.macKey() * secret;
    for(int i = 1; i < nP; i++) {
      rgen.pi(i).random_data(&val, sizeof(Field));
      
      tpShare.pushValues(val);
      rgen.pi(i).random_data(&tag, sizeof(Field));
      
      tpShare.pushTags(tag);
      valn += val;
      tagn += tag;
    }
    valn = secret - valn;
    tagn = tagF - tagn;
    network.send(nP, &valn, sizeof(Field));
    network.send(nP, &tagn, sizeof(Field));
    tpShare.pushValues(valn);
    tpShare.pushTags(tagn);
  }
  else if ( pid > 0) {
    if(pid == dealer) {
      rgen.p0().random_data(&secret, sizeof(Field));
      
    }
    if(pid != nP) {
      rgen.p0().random_data(&val, sizeof(Field));
      share.pushValue(val);
      rgen.p0().random_data(&tag, sizeof(Field)); 
      share.pushTag(tag);
    }
    else if (pid == nP) {
      network.recv(0, &valn, sizeof(Field));
      share.pushValue(valn);
      network.recv(0, &tagn, sizeof(Field));
      share.pushTag(tagn);
    }
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
          randomShareWithParty(nP_, id_, pid, rgen_, *network_, pregate->mask, 
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
          randomShareWithParty(nP_, id_, 0, rgen_, *network_, 
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

void OfflineEvaluator::getOutputMasks(int pid, std::vector<Field>& output_mask) { 
  output_mask.clear();
  if(circ_.outputs.empty()) {
    return;
  }
  preproc_.output.resize(circ_.outputs.size());
  
  if(pid == 0){
    for(size_t i = 0; i < circ_.outputs.size(); i++) {
      //auto& preout = preproc_.output[i];
      output_mask.push_back(preproc_.gates[circ_.outputs[i]]->tpmask.secret());
    }
    
  }
  else {
    output_mask.push_back(0);
  }
  
}

PreprocCircuit<Field> OfflineEvaluator::getPreproc() {
  return std::move(preproc_);
}

PreprocCircuit<Field> OfflineEvaluator::run(
    const std::unordered_map<quadsquad::utils::wire_t, int>& input_pid_map) {
  setWireMasks(input_pid_map);
  std::cout<<"INSIDE OfflineEvaluator:run"<<std::endl;
  std::vector<Field> output_mask;
  std::cout<<"Printing output masks"<<std::endl;
  getOutputMasks(id_, output_mask);
  for(size_t i = 0; i < circ_.outputs.size(); i++) {
    std::cout<<i<<": "<< output_mask[i] <<std::endl;
  }
  preproc_.output = output_mask;

  return std::move(preproc_);
  
}


};  // namespace dirigent
