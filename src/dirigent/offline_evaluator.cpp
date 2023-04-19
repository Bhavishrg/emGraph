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
      preproc_(circ.num_gates)

      {tpool_ = std::make_shared<ThreadPool>(threads);}

void OfflineEvaluator::keyGen(int nP, int pid, RandGenPool& rgen, 
                      std::vector<Field>& keySh, Field& key)  {
  
  if(pid == 0) {
    key = 0;
    keySh[0] = 0;
    for(int i = 1; i <= nP; i++) {
        rgen.pi(i).random_data(&keySh[i], sizeof(Field));
        key += keySh[i];
    }
  }
  else {
    rgen.p0().random_data(&key, sizeof(Field));
  }
}



void OfflineEvaluator::randomShare(int nP, int pid, RandGenPool& rgen, io::NetIOMP& network,
                                    AuthAddShare<Field>& share, TPShare<Field>& tpShare,
                                    Field key, std::vector<Field> keySh, std::vector<Field>& rand_sh, 
                                    size_t& idx_rand_sh) {
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
      share.setKey(keySh[0]);
      tpShare.pushValues(0);
      tpShare.pushTags(0);
      tpShare.setKeySh(keySh[0]);
      tpShare.setKey(key);
      
      for(int i = 1; i <= nP; i++) {

        rgen.pi(i).random_data(&val, sizeof(Field));
        tpShare.pushValues(val);
        tpShare.setKeySh(keySh[i]);
        rgen.pi(i).random_data(&tag, sizeof(Field));
        if( i != nP) {
          tpShare.pushTags(tag);
          tagn += tag;
          
        }
      }
      Field secret = tpShare.secret();
      
      tag = key * secret;
      tagn = tag - tagn;
      tpShare.pushTags(tagn);
      rand_sh.push_back(tagn);
    }
    else if(pid > 0) {
      share.setKey(key);
      rgen.p0().random_data(&val, sizeof(Field));
      share.pushValue(val);
      
      rgen.p0().random_data(&tag, sizeof(Field));
      
      if( pid != nP) {
        share.pushTag(tag);
      }
      else if(pid == nP) {

        share.pushTag(rand_sh[idx_rand_sh]);
        idx_rand_sh++;
      }
    }

}

void OfflineEvaluator::randomShareSecret(int nP, int pid, RandGenPool& rgen, io::NetIOMP& network,
                                  AuthAddShare<Field>& share, TPShare<Field>& tpShare,
                                  Field secret, Field key, std::vector<Field> keySh, 
                                  std::vector<Field>& rand_sh_sec, size_t& idx_rand_sh_sec) {
  Field val = 0;
  Field tag = 0;
  Field tagn = 0;
  Field valn = 0;
  
    if(pid == 0) {
      share.pushValue(0);
      share.pushTag(0);
      share.setKey(keySh[0]);
      tpShare.pushValues(0);
      tpShare.pushTags(0);
      tpShare.setKeySh(keySh[0]);
      tpShare.setKey(key);
      for(int i = 1; i < nP; i++) {
        rgen.pi(i).random_data(&val, sizeof(Field));
        tpShare.pushValues(val);
        valn += val;
        rgen.pi(i).random_data(&tag, sizeof(Field));
        tpShare.pushTags(tag);
        tagn += tag;
        tpShare.setKeySh(keySh[i]);
      }
      valn = secret - valn;
      tagn = key * secret - tagn;
      tpShare.pushValues(valn);
      tpShare.pushTags(tagn);
      rand_sh_sec.push_back(valn);
      rand_sh_sec.push_back(tagn);
    }
    else if(pid > 0) {
      share.setKey(key);
      if( pid != nP) {
        rgen.p0().random_data(&val, sizeof(Field));
        share.pushValue(val);
        rgen.p0().random_data(&tag, sizeof(Field));
        share.pushTag(tag);
      }
      else if(pid == nP) {
        valn = rand_sh_sec[idx_rand_sh_sec];
        idx_rand_sh_sec++;
        tagn = rand_sh_sec[idx_rand_sh_sec];
        idx_rand_sh_sec++;
        share.pushValue(valn);
        share.pushTag(tagn);
      }
    }
}

void OfflineEvaluator::randomShareWithParty(int nP, int pid, int dealer, RandGenPool& rgen,
                                            io::NetIOMP& network, AuthAddShare<Field>& share,
                                            TPShare<Field>& tpShare, Field& secret, Field key,
                                            std::vector<Field> keySh, std::vector<Field>& rand_sh_party, 
                                            size_t& idx_rand_sh_party) {
                                             
                                            
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
    share.setKey(keySh[0]);
    tpShare.pushValues(0);
    tpShare.pushTags(0);
    tpShare.setKeySh(keySh[0]);
    tpShare.setKey(key);
    
    tagF = key * secret;
    for(int i = 1; i < nP; i++) {
      tpShare.setKeySh(keySh[i]);
      rgen.pi(i).random_data(&val, sizeof(Field));
      
      tpShare.pushValues(val);
      rgen.pi(i).random_data(&tag, sizeof(Field));
      
      tpShare.pushTags(tag);
      valn += val;
      tagn += tag;
    }
    tpShare.setKeySh(keySh[nP]);
    valn = secret - valn;
    tagn = tagF - tagn;
    rand_sh_party.push_back(valn);
    rand_sh_party.push_back(tagn);
    
    tpShare.pushValues(valn);
    tpShare.pushTags(tagn);
  }
  else if ( pid > 0) {
    share.setKey(key);
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
      valn = rand_sh_party[idx_rand_sh_party];
      idx_rand_sh_party++;
      tagn = rand_sh_party[idx_rand_sh_party];
      idx_rand_sh_party++;
      share.pushValue(valn);
      share.pushTag(tagn);
    }
  }

}

void OfflineEvaluator::setWireMasksParty(
  const std::unordered_map<quadsquad::utils::wire_t, int>& input_pid_map, 
  std::vector<Field>& rand_sh, std::vector<Field>& rand_sh_sec, 
  std::vector<Field>& rand_sh_party) {

    
      size_t idx_rand_sh = 0;
      
      
      size_t idx_rand_sh_sec = 0;

    
      size_t idx_rand_sh_party = 0;

    // key setup
      std::vector<Field> keySh(nP_ + 1);
      Field key = 0;
      if(id_ == 0)  {
        key = 0;
        keySh[0] = 0;
        for(int i = 1; i <= nP_; i++) {
            rgen_.pi(i).random_data(&keySh[i], sizeof(Field));
            key += keySh[i];
        }
        key_sh_ = key;
      }
      else {
        rgen_.p0().random_data(&key, sizeof(Field));
        key_sh_ = key;
      }
    for (const auto& level : circ_.gates_by_level) {
    for (const auto& gate : level) {
      switch (gate->type) {
        case quadsquad::utils::GateType::kInp: {
          auto pregate = std::make_unique<PreprocInput<Field>>();

          auto pid = input_pid_map.at(gate->out);
          pregate->pid = pid;
          randomShareWithParty(nP_, id_, pid, rgen_, *network_, pregate->mask, 
                              pregate->tpmask, pregate->mask_value, key, keySh, rand_sh_party, idx_rand_sh_party);

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
          Field tp_prod;
          if(id_ == 0) {tp_prod = tpmask_in1.secret() * tpmask_in2.secret();}
          TPShare<Field> tprand_mask;
          AuthAddShare<Field> rand_mask;
          randomShare(nP_, id_, rgen_, *network_, rand_mask, tprand_mask, key, keySh, rand_sh, idx_rand_sh);
                    
          TPShare<Field> tpmask_product;
          AuthAddShare<Field> mask_product; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                                mask_product, tpmask_product, tp_prod, key, keySh, rand_sh_sec, idx_rand_sh_sec);

          preproc_.gates[gate->out] = std::move(std::make_unique<PreprocMultGate<Field>>
                              (rand_mask, tprand_mask, mask_product, tpmask_product));
          
          break;
        }

        case quadsquad::utils::GateType::kMul3: {
          preproc_.gates[gate->out] = std::make_unique<PreprocMult3Gate<Field>>();
          const auto* g = static_cast<quadsquad::utils::FIn3Gate*>(gate.get());
          
          const auto& mask_in1 = preproc_.gates[g->in1]->mask;
          const auto& tpmask_in1 = preproc_.gates[g->in1]->tpmask;

          const auto& mask_in2 = preproc_.gates[g->in2]->mask;
          const auto& tpmask_in2 = preproc_.gates[g->in2]->tpmask;

          const auto& mask_in3 = preproc_.gates[g->in3]->mask;
          const auto& tpmask_in3 = preproc_.gates[g->in3]->tpmask;

          Field tp_ab, tp_ac, tp_bc, tp_abc;
          
          if(id_ == 0) {
            tp_ab = tpmask_in1.secret() * tpmask_in2.secret();
            tp_ac = tpmask_in1.secret() * tpmask_in3.secret();
            tp_bc = tpmask_in2.secret() * tpmask_in3.secret();
          
            tp_abc = tpmask_in1.secret() * tpmask_in2.secret() * tpmask_in3.secret();
          }

          TPShare<Field> tprand_mask;
          AuthAddShare<Field> rand_mask;
          randomShare(nP_, id_, rgen_, *network_, 
                                  rand_mask, tprand_mask, key, keySh, rand_sh, idx_rand_sh);
          

          TPShare<Field> tpmask_ab;
          AuthAddShare<Field> mask_ab; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                                mask_ab, tpmask_ab, tp_ab, key, keySh, rand_sh_sec, idx_rand_sh_sec);

          TPShare<Field> tpmask_ac;
          AuthAddShare<Field> mask_ac; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                                mask_ac, tpmask_ac, tp_ac, key, keySh, rand_sh_sec, idx_rand_sh_sec);
          
          TPShare<Field> tpmask_bc;
          AuthAddShare<Field> mask_bc; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                                mask_bc, tpmask_bc, tp_bc, key, keySh, rand_sh_sec, idx_rand_sh_sec);
                    
          TPShare<Field> tpmask_abc;
          AuthAddShare<Field> mask_abc; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                                mask_abc, tpmask_abc, tp_abc, key, keySh, rand_sh_sec, idx_rand_sh_sec);

          preproc_.gates[gate->out] = std::move(std::make_unique<PreprocMult3Gate<Field>>
                              (rand_mask, tprand_mask, 
                              mask_ab, tpmask_ab, 
                              mask_ac, tpmask_ac,
                              mask_bc, tpmask_bc, 
                              mask_abc, tpmask_abc));
          break;
        }

        case quadsquad::utils::GateType::kMul4: {
          preproc_.gates[gate->out] = std::make_unique<PreprocMult4Gate<Field>>();
          const auto* g = static_cast<quadsquad::utils::FIn4Gate*>(gate.get());

          const auto& mask_in1 = preproc_.gates[g->in1]->mask;
          const auto& tpmask_in1 = preproc_.gates[g->in1]->tpmask;

          const auto& mask_in2 = preproc_.gates[g->in2]->mask;
          const auto& tpmask_in2 = preproc_.gates[g->in2]->tpmask;

          const auto& mask_in3 = preproc_.gates[g->in3]->mask;
          const auto& tpmask_in3 = preproc_.gates[g->in3]->tpmask;

          const auto& mask_in4 = preproc_.gates[g->in4]->mask;
          const auto& tpmask_in4 = preproc_.gates[g->in4]->tpmask;

          Field tp_ab, tp_ac, tp_ad, tp_bc, tp_bd, tp_cd, tp_abc, tp_abd, tp_acd, tp_bcd, tp_abcd;
          if(id_ == 0) {
            tp_ab = tpmask_in1.secret() * tpmask_in2.secret();
            tp_ac = tpmask_in1.secret() * tpmask_in3.secret();
            tp_ad = tpmask_in1.secret() * tpmask_in4.secret();
            tp_bc = tpmask_in2.secret() * tpmask_in3.secret();
            tp_bd = tpmask_in2.secret() * tpmask_in4.secret();
            tp_cd = tpmask_in3.secret() * tpmask_in4.secret();
            tp_abc = tpmask_in1.secret() * tpmask_in2.secret() * tpmask_in3.secret();
            tp_abd = tpmask_in1.secret() * tpmask_in2.secret() * tpmask_in4.secret();
            tp_acd = tpmask_in1.secret() * tpmask_in3.secret() * tpmask_in4.secret();
            tp_bcd = tpmask_in2.secret() * tpmask_in3.secret() * tpmask_in4.secret();
            tp_abcd = tpmask_in1.secret() * tpmask_in2.secret() 
                        * tpmask_in3.secret() * tpmask_in4.secret();
          }

          TPShare<Field> tprand_mask;
          AuthAddShare<Field> rand_mask;
          randomShare(nP_, id_, rgen_, *network_, 
                          rand_mask, tprand_mask, key, keySh, rand_sh, idx_rand_sh);
          idx_rand_sh++;
          TPShare<Field> tpmask_ab;
          AuthAddShare<Field> mask_ab; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                          mask_ab, tpmask_ab, tp_ab, key, keySh, rand_sh_sec, idx_rand_sh_sec);

          
          TPShare<Field> tpmask_ac;
          AuthAddShare<Field> mask_ac; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                          mask_ac, tpmask_ac, tp_ac, key, keySh, rand_sh_sec, idx_rand_sh_sec);

          TPShare<Field> tpmask_ad;
          AuthAddShare<Field> mask_ad; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                          mask_ad, tpmask_ad, tp_ad, key, keySh, rand_sh_sec, idx_rand_sh_sec);

          TPShare<Field> tpmask_bc;
          AuthAddShare<Field> mask_bc; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                            mask_bc, tpmask_bc, tp_bc, key, keySh, rand_sh_sec, idx_rand_sh_sec);

          TPShare<Field> tpmask_bd;
          AuthAddShare<Field> mask_bd; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                            mask_bd, tpmask_bd, tp_bd, key, keySh, rand_sh_sec, idx_rand_sh_sec);
        
        
          TPShare<Field> tpmask_cd;
          AuthAddShare<Field> mask_cd; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                            mask_cd, tpmask_cd, tp_cd, key, keySh, rand_sh_sec, idx_rand_sh_sec);

          TPShare<Field> tpmask_abc;
          AuthAddShare<Field> mask_abc; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                            mask_abc, tpmask_abc, tp_abc, key, keySh, rand_sh_sec, idx_rand_sh_sec);
          
          TPShare<Field> tpmask_abd;
          AuthAddShare<Field> mask_abd; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                            mask_abd, tpmask_abd, tp_abd, key, keySh, rand_sh_sec, idx_rand_sh_sec);
        
          TPShare<Field> tpmask_acd;
          AuthAddShare<Field> mask_acd; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                            mask_acd, tpmask_acd, tp_acd, key, keySh, rand_sh_sec, idx_rand_sh_sec);

          TPShare<Field> tpmask_bcd;
          AuthAddShare<Field> mask_bcd; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                            mask_bcd, tpmask_bcd, tp_bcd, key, keySh, rand_sh_sec, idx_rand_sh_sec);

          TPShare<Field> tpmask_abcd;
          AuthAddShare<Field> mask_abcd; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                            mask_abcd, tpmask_abcd, tp_abcd, key, keySh, rand_sh_sec, idx_rand_sh_sec);    

          preproc_.gates[gate->out] = std::move(std::make_unique<PreprocMult4Gate<Field>>
                              (rand_mask, tprand_mask, 
                              mask_ab, tpmask_ab,
                              mask_ac, tpmask_ac, 
                              mask_ad, tpmask_ad, 
                              mask_bc, tpmask_bc,
                              mask_bd, tpmask_bd,
                              mask_cd, tpmask_cd,
                              mask_abc, tpmask_abc,
                              mask_abd, tpmask_abd,
                              mask_acd, tpmask_acd,
                              mask_bcd, tpmask_bcd,
                              mask_abcd, tpmask_abcd));
          break;    
        }

        case quadsquad::utils::GateType::kDotprod: {
          preproc_.gates[gate->out] = std::make_unique<PreprocDotpGate<Field>>();
          const auto* g = static_cast<quadsquad::utils::SIMDGate*>(gate.get());
          Field mask_prod = 0;
          if(id_ ==0) {
            for(size_t i = 0; i < g->in1.size(); i++) {
              mask_prod += preproc_.gates[g->in1[i]]->tpmask.secret() 
                                * preproc_.gates[g->in2[i]]->tpmask.secret();
            }
          }
          TPShare<Field> tprand_mask;
          AuthAddShare<Field> rand_mask;
          randomShare(nP_, id_, rgen_, *network_, rand_mask, tprand_mask, key, keySh, rand_sh, idx_rand_sh);
        

          TPShare<Field> tpmask_product;
          AuthAddShare<Field> mask_product; 
          randomShareSecret(nP_, id_, rgen_, *network_, 
                                mask_product, tpmask_product, mask_prod, key, keySh, rand_sh_sec, idx_rand_sh_sec);
                                
          preproc_.gates[gate->out] = std::move(std::make_unique<PreprocDotpGate<Field>>
                              (rand_mask, tprand_mask, mask_product, tpmask_product));
          
          break;
        }
        
        default: {
          break;
        }
      }
    }
  }
}


void OfflineEvaluator::setWireMasks(
    const std::unordered_map<quadsquad::utils::wire_t, int>& input_pid_map) {
      
      std::vector<Field> rand_sh;
      size_t idx_rand_sh;
      
      std::vector<Field> rand_sh_sec;
      size_t idx_rand_sh_sec;

      std::vector<Field> rand_sh_party;
      size_t idx_rand_sh_party;


      
  if(id_ != nP_) {
    setWireMasksParty(input_pid_map, rand_sh, rand_sh_sec, rand_sh_party);

  
    if(id_ == 0) {
      size_t rand_sh_num = rand_sh.size();
      size_t rand_sh_sec_num = rand_sh_sec.size();
      size_t rand_sh_party_num = rand_sh_party.size();
      size_t total_comm = rand_sh_num + rand_sh_sec_num + rand_sh_party_num;
      std::vector<size_t> lengths(4);
      lengths[0] = total_comm;
      lengths[1] = rand_sh_num;
      lengths[2] = rand_sh_sec_num;
      lengths[3] = rand_sh_party_num;


      network_->send(nP_, lengths.data(), sizeof(size_t) * 4);

      std::vector<Field> offline_comm(total_comm);
      for(size_t i = 0; i < rand_sh_num; i++) {
        offline_comm[i] = rand_sh[i];
      }
      for(size_t i = 0; i < rand_sh_sec_num; i++) {
        offline_comm[rand_sh_num + i] = rand_sh_sec[i];
      }
      for(size_t i = 0; i < rand_sh_party_num; i++) {
        offline_comm[rand_sh_sec_num + rand_sh_num + i] = rand_sh_party[i];
      }
      network_->send(nP_, offline_comm.data(), sizeof(Field) * total_comm);
    }
  }
  else if(id_ == nP_ ) {
    std::vector<size_t> lengths(4);
    
    network_->recv(0, lengths.data(), sizeof(size_t) * 4);
    
    size_t total_comm = lengths[0];
    size_t rand_sh_num = lengths[1];
    size_t rand_sh_sec_num = lengths[2];
    size_t rand_sh_party_num = lengths[3];

    

    std::vector<Field> offline_comm(total_comm);

    network_->recv(0, offline_comm.data(), sizeof(Field) * total_comm);
    
    rand_sh.resize(rand_sh_num);
    
    for(int i = 0; i < rand_sh_num; i++) {
      rand_sh[i] = offline_comm[i];
    }

    rand_sh_sec.resize(rand_sh_sec_num);
    
    for(int i = 0; i < rand_sh_sec_num; i++) {
      rand_sh_sec[i] = offline_comm[rand_sh_num + i];
    }
    
    rand_sh_party.resize(rand_sh_party_num);
    
    for(int i = 0; i < rand_sh_party_num; i++) {
      rand_sh_party[i] = offline_comm[rand_sh_num + rand_sh_sec_num + i];
    }


    setWireMasksParty(input_pid_map, rand_sh, rand_sh_sec, rand_sh_party);
  }
  
}

void OfflineEvaluator::getOutputMasks(int pid, std::vector<Field>& output_mask) { 
  output_mask.clear();
  if(circ_.outputs.empty()) {
    return;
  }
  
  
  if(pid == 0){
    for(size_t i = 0; i < circ_.outputs.size(); i++) {
      output_mask.push_back(preproc_.gates[circ_.outputs[i]]->tpmask.secret());
    }
    
  }
  else {
    for(size_t i = 0; i < circ_.outputs.size(); i++) {
    output_mask.push_back(0);
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
