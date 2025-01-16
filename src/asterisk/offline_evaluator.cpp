#include "offline_evaluator.h"

#include <NTL/BasicThreadPool.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <thread>

// #include "../utils/helpers.h"

namespace asterisk {
OfflineEvaluator::OfflineEvaluator(int nP, int my_id,
                                   std::shared_ptr<io::NetIOMP> network,
                                   common::utils::LevelOrderedCircuit circ,
                                   int threads, int seed)
    : nP_(nP),
      id_(my_id),
      rgen_(my_id, seed), 
      network_(std::move(network)),
      circ_(std::move(circ))
      // preproc_(circ.num_gates)

      { tpool_ = std::make_shared<ThreadPool>(threads); }

void OfflineEvaluator::randomShare(int nP, int pid, RandGenPool& rgen, AddShare<Ring>& share, TPShare<Ring>& tpShare) {
  Ring val = Ring(0);
  if (pid == 0) {
    share.pushValue(Ring(0));
    tpShare.pushValues(Ring(0));
    for (int i = 1; i <= nP; i++) {
      rgen.pi(i).random_data(&val, sizeof(Ring)); // TODO: Check if this is the key common b/w P_i and HP only
      tpShare.pushValues(val);
    }
  } else {
    rgen.p0().random_data(&val, sizeof(Ring)); // TODO: Check if this is the key common b/w P_i and HP only
    share.pushValue(val);
  }
}

void OfflineEvaluator::randomShareSecret(int nP, int pid, RandGenPool& rgen,
                                         AddShare<Ring>& share, TPShare<Ring>& tpShare, Ring secret,
                                         std::vector<Ring>& rand_sh_sec, size_t& idx_rand_sh_sec) {
  if (pid == 0) {
    Ring val = Ring(0);
    Ring valn = Ring(0);
    share.pushValue(Ring(0));
    tpShare.pushValues(Ring(0));
    for (int i = 1; i < nP; i++) {
      rgen.pi(i).random_data(&val, sizeof(Ring)); // TODO: Check if this is the key common b/w P_i and HP only
      tpShare.pushValues(val);
      valn += val;
    }
    valn = secret - val;
    tpShare.pushValues(valn);
    rand_sh_sec.push_back(valn);
  } else {
    if (pid != nP) {
      Ring val;
      rgen.p0().random_data(&val, sizeof(Ring)); // TODO: Check if this is the key common b/w P_i and HP only
      share.pushValue(val);
    } else {
      share.pushValue(rand_sh_sec[idx_rand_sh_sec]);
      idx_rand_sh_sec++;
    }
  }
}

// TODO: change to random permutations from identity permutations
void OfflineEvaluator::randomPermutation(int nP, int pid, RandGenPool& rgen,
                                         std::vector<int>& pi, std::vector<std::vector<int>>& tp_pi_all, size_t& vec_size) {
  if (pid == 0) {
    std::vector<int> rand_pi(vec_size);
    for (int i = 0; i < nP; ++i) {
      for (int j = 0; j < vec_size; ++j) {
        rand_pi[j] = j; // sample using key common to HP and P_i
      }
      tp_pi_all[i] = rand_pi;
    }
  } else {
    for (int i = 0; i < vec_size; ++i) {
      pi[i] = i; // sample using key common to HP and P_i or common to all P_i except for HP if computing pi_common
    }
  }
}

void OfflineEvaluator::generateShuffleDeltaVector(int nP, int pid, RandGenPool& rgen, std::vector<AddShare<Ring>>& delta,
                                                  std::vector<TPShare<Ring>>& tp_a, std::vector<TPShare<Ring>>& tp_b,
                                                  std::vector<TPShare<Ring>>& tp_c, std::vector<std::vector<int>>& tp_pi_all,
                                                  size_t& vec_size, std::vector<Ring>& rand_sh_sec, size_t& idx_rand_sh_sec) {
  if (pid == 0) {
    std::vector<Ring> deltan(vec_size);
    Ring valn;
    for (int i = 0; i < vec_size; ++i) {
      Ring val_a = tp_a[i].secret() - tp_a[i][1];
      int idx_perm = i;
      for (int j = 0; j < nP; ++j) {
        idx_perm = tp_pi_all[j][idx_perm];
        val_a += tp_c[idx_perm][j + 1];
      }
      Ring val_b = tp_b[idx_perm].secret() - tp_b[idx_perm][nP];
      deltan[idx_perm] = val_a - tp_c[idx_perm][nP] - val_b;
    }
    for (int i = 0; i < vec_size; ++i) {
      rand_sh_sec.push_back(deltan[i]);
    }
  } else if (pid == nP) {
    for (int i = 0; i < vec_size; ++i) {
      delta[i].pushValue(rand_sh_sec[idx_rand_sh_sec]);
      idx_rand_sh_sec++;
    }
  }
}

void OfflineEvaluator::generatePermAndShDeltaVector(int nP, int pid, RandGenPool& rgen, int owner, std::vector<AddShare<Ring>>& delta,
                                                    std::vector<TPShare<Ring>>& tp_a, std::vector<TPShare<Ring>>& tp_b,
                                                    std::vector<int>& pi, size_t& vec_size, std::vector<Ring>& delta_sh, size_t& idx_delta_sh) {
  if (pid == 0) {
    std::vector<Ring> deltan(vec_size);
    for (int i = 0; i < vec_size; ++i) {
      Ring val_a = tp_a[i].secret() - tp_a[i][owner];
      int idx_perm = pi[i];
      Ring val_b = tp_b[idx_perm].secret() - tp_b[idx_perm][owner];
      deltan[idx_perm] = val_a - val_b;
    }
    for (int i = 0; i < vec_size; ++i) {
      delta_sh.push_back(deltan[i]);
    }
  } else if (pid == owner) {
    for (int i = 0; i < vec_size; ++i) {
      delta[i].pushValue(delta_sh[idx_delta_sh]);
      idx_delta_sh++;
    }
  }
}

void OfflineEvaluator::setWireMasksParty(const std::unordered_map<common::utils::wire_t, int>& input_pid_map, 
                                         std::vector<Ring>& rand_sh_sec, std::vector<BoolRing>& b_rand_sh_sec,
                                         std::vector<std::vector<Ring>>& delta_sh, size_t& vec_size) {
  size_t idx_rand_sh_sec = 0;
  size_t idx_delta_sh = 0;
  size_t b_idx_rand_sh_sec = 0;

  for (const auto& level : circ_.gates_by_level) {
    for (const auto& gate : level) {
      switch (gate->type) {
        case common::utils::GateType::kInp: {
          auto pregate = std::make_unique<PreprocInput<Ring>>();
          auto pid = input_pid_map.at(gate->out);
          pregate->pid = pid;
          preproc_.gates[gate->out] = std::move(pregate);
          break;
        }

        case common::utils::GateType::kMul: {
          AddShare<Ring> triple_a; // Holds one beaver triple share of a random value a
          TPShare<Ring> tp_triple_a; // Holds all the beaver triple shares of a random value a
          AddShare<Ring> triple_b; // Holds one beaver triple share of a random value b
          TPShare<Ring> tp_triple_b; // Holds all the beaver triple shares of a random value b
          AddShare<Ring> triple_c; // Holds one beaver triple share of c=a*b
          TPShare<Ring> tp_triple_c; // Holds all the beaver triple shares of c=a*b
          randomShare(nP_, id_, rgen_, triple_a, tp_triple_a);
          randomShare(nP_, id_, rgen_, triple_b, tp_triple_b);
          Ring tp_prod;
          if (id_ == 0) { tp_prod = tp_triple_a.secret() * tp_triple_b.secret(); }
          randomShareSecret(nP_, id_, rgen_, triple_c, tp_triple_c, tp_prod, rand_sh_sec, idx_rand_sh_sec);
          preproc_.gates[gate->out] =
              std::move(std::make_unique<PreprocMultGate<Ring>>(triple_a, tp_triple_a, triple_b, tp_triple_b, triple_c, tp_triple_c));
          break;
        }

        case common::utils::GateType::kMul3: {
          AddShare<Ring> share_a; // Holds one share of a random value a
          TPShare<Ring> tp_share_a; // Holds all the shares of a random value a
          AddShare<Ring> share_b; // Holds one of a random value b
          TPShare<Ring> tp_share_b; // Holds all the shares of a random value b
          AddShare<Ring> share_c; // Holds one share of a random value c
          TPShare<Ring> tp_share_c; // Holds all the shares of a random value c
          AddShare<Ring> share_ab; // Holds one share of a*b
          TPShare<Ring> tp_share_ab; // Holds all the shares of a*b
          AddShare<Ring> share_bc; // Holds one share of b*c
          TPShare<Ring> tp_share_bc; // Holds all the shares of b*c
          AddShare<Ring> share_ca; // Holds one share of c*a
          TPShare<Ring> tp_share_ca; // Holds all the shares of c*a
          AddShare<Ring> share_abc; // Holds one share of a*b*c
          TPShare<Ring> tp_share_abc; // Holds all the shares of a*b*c
          randomShare(nP_, id_, rgen_, share_a, tp_share_a);
          randomShare(nP_, id_, rgen_, share_b, tp_share_b);
          randomShare(nP_, id_, rgen_, share_c, tp_share_c);
          Ring tp_ab, tp_bc, tp_ca, tp_abc;
          if (id_ == 0) {
            tp_ab = tp_share_a.secret() * tp_share_b.secret();
            tp_bc = tp_share_b.secret() * tp_share_c.secret();
            tp_ca = tp_share_c.secret() * tp_share_a.secret();
            tp_abc = tp_share_a.secret() * tp_share_b.secret() * tp_share_c.secret();
          }
          randomShareSecret(nP_, id_, rgen_, share_ab, tp_share_ab, tp_ab, rand_sh_sec, idx_rand_sh_sec);
          randomShareSecret(nP_, id_, rgen_, share_bc, tp_share_bc, tp_bc, rand_sh_sec, idx_rand_sh_sec);
          randomShareSecret(nP_, id_, rgen_, share_ca, tp_share_ca, tp_ca, rand_sh_sec, idx_rand_sh_sec);
          randomShareSecret(nP_, id_, rgen_, share_abc, tp_share_abc, tp_abc, rand_sh_sec, idx_rand_sh_sec);
          preproc_.gates[gate->out] =
              std::move(std::make_unique<PreprocMult3Gate<Ring>>(share_a, tp_share_a, share_b, tp_share_b, share_c, tp_share_c,
                                                                 share_ab, tp_share_ab, share_bc, tp_share_bc, share_ca, tp_share_ca,
                                                                 share_abc, tp_share_abc));
          break;
        }

        case common::utils::GateType::kMul4: {
          AddShare<Ring> share_a; // Holds one share of a random value a
          TPShare<Ring> tp_share_a; // Holds all the shares of a random value a
          AddShare<Ring> share_b; // Holds one of a random value b
          TPShare<Ring> tp_share_b; // Holds all the shares of a random value b
          AddShare<Ring> share_c; // Holds one share of a random value c
          TPShare<Ring> tp_share_c; // Holds all the shares of a random value c
          AddShare<Ring> share_d; // Holds one share of a random value d
          TPShare<Ring> tp_share_d; // Holds all the shares of a random value d
          AddShare<Ring> share_ab; // Holds one share of a*b
          TPShare<Ring> tp_share_ab; // Holds all the shares of a*b
          AddShare<Ring> share_ac; // Holds one share of a*c
          TPShare<Ring> tp_share_ac; // Holds all the shares of a*c
          AddShare<Ring> share_ad; // Holds one share of a*d
          TPShare<Ring> tp_share_ad; // Holds all the shares of a*d
          AddShare<Ring> share_bc; // Holds one share of b*c
          TPShare<Ring> tp_share_bc; // Holds all the shares of b*c
          AddShare<Ring> share_bd; // Holds one share of b*d
          TPShare<Ring> tp_share_bd; // Holds all the shares of b*d
          AddShare<Ring> share_cd; // Holds one share of c*d
          TPShare<Ring> tp_share_cd; // Holds all the shares of c*d
          AddShare<Ring> share_abc; // Holds one share of a*b*c
          TPShare<Ring> tp_share_abc; // Holds all the shares of a*b*c
          AddShare<Ring> share_abd; // Holds one share of a*b*d
          TPShare<Ring> tp_share_abd; // Holds all the shares of a*b*d
          AddShare<Ring> share_acd; // Holds one share of a*c*d
          TPShare<Ring> tp_share_acd; // Holds all the shares of a*c*d
          AddShare<Ring> share_bcd; // Holds one share of b*c*d
          TPShare<Ring> tp_share_bcd; // Holds all the shares of b*c*d
          AddShare<Ring> share_abcd; // Holds one share of a*b*c*d
          TPShare<Ring> tp_share_abcd; // Holds all the shares of a*b*c*d
          randomShare(nP_, id_, rgen_, share_a, tp_share_a);
          randomShare(nP_, id_, rgen_, share_b, tp_share_b);
          randomShare(nP_, id_, rgen_, share_c, tp_share_c);
          randomShare(nP_, id_, rgen_, share_d, tp_share_d);
          Ring tp_ab, tp_ac, tp_ad, tp_bc, tp_bd, tp_cd, tp_abc, tp_abd, tp_acd, tp_bcd, tp_abcd;
          if (id_ == 0) {
            tp_ab = tp_share_a.secret() * tp_share_b.secret();
            tp_ac = tp_share_a.secret() * tp_share_c.secret();
            tp_ad = tp_share_a.secret() * tp_share_d.secret();
            tp_bc = tp_share_b.secret() * tp_share_c.secret();
            tp_bd = tp_share_b.secret() * tp_share_d.secret();
            tp_cd = tp_share_c.secret() * tp_share_d.secret();
            tp_abc = tp_share_a.secret() * tp_share_b.secret() * tp_share_c.secret();
            tp_abd = tp_share_a.secret() * tp_share_b.secret() * tp_share_d.secret();
            tp_acd = tp_share_a.secret() * tp_share_c.secret() * tp_share_d.secret();
            tp_bcd = tp_share_b.secret() * tp_share_c.secret() * tp_share_d.secret();
            tp_abcd = tp_share_a.secret() * tp_share_b.secret() * tp_share_c.secret() * tp_share_d.secret();
          }
          randomShareSecret(nP_, id_, rgen_, share_ab, tp_share_ab, tp_ab, rand_sh_sec, idx_rand_sh_sec);
          randomShareSecret(nP_, id_, rgen_, share_ac, tp_share_ac, tp_ac, rand_sh_sec, idx_rand_sh_sec);
          randomShareSecret(nP_, id_, rgen_, share_ad, tp_share_ad, tp_ad, rand_sh_sec, idx_rand_sh_sec);
          randomShareSecret(nP_, id_, rgen_, share_bc, tp_share_bc, tp_bc, rand_sh_sec, idx_rand_sh_sec);
          randomShareSecret(nP_, id_, rgen_, share_bd, tp_share_bd, tp_bd, rand_sh_sec, idx_rand_sh_sec);
          randomShareSecret(nP_, id_, rgen_, share_cd, tp_share_cd, tp_cd, rand_sh_sec, idx_rand_sh_sec);
          randomShareSecret(nP_, id_, rgen_, share_abc, tp_share_abc, tp_abc, rand_sh_sec, idx_rand_sh_sec);
          randomShareSecret(nP_, id_, rgen_, share_abd, tp_share_abd, tp_abd, rand_sh_sec, idx_rand_sh_sec);
          randomShareSecret(nP_, id_, rgen_, share_acd, tp_share_acd, tp_acd, rand_sh_sec, idx_rand_sh_sec);
          randomShareSecret(nP_, id_, rgen_, share_bcd, tp_share_bcd, tp_bcd, rand_sh_sec, idx_rand_sh_sec);
          randomShareSecret(nP_, id_, rgen_, share_abcd, tp_share_abcd, tp_abcd, rand_sh_sec, idx_rand_sh_sec);
          preproc_.gates[gate->out] =
              std::move(std::make_unique<PreprocMult4Gate<Ring>>(share_a, tp_share_a, share_b, tp_share_b, share_c, tp_share_c,
                                                                 share_d, tp_share_d, share_ab, tp_share_ab, share_ac, tp_share_ac,
                                                                 share_ad, tp_share_ad, share_bc, tp_share_bc, share_bd, tp_share_bd,
                                                                 share_cd, tp_share_cd, share_abc, tp_share_abc, share_abd, tp_share_abd,
                                                                 share_acd, tp_share_acd, share_bcd, tp_share_bcd, share_abcd, tp_share_abcd));
          break;
        }

        case common::utils::GateType::kEqz: {
          AddShare<Ring> share_r;
          TPShare<Ring> tp_share_r;
          std::vector<AddShare<BoolRing>> share_r_bits(RINGSIZEBITS);
          std::vector<TPShare<BoolRing>> tp_share_r_bits(RINGSIZEBITS);
          randomShare(nP_, id_, rgen_, share_r, tp_share_r);
          Ring tp_r = Ring(0);
          std::vector<BoolRing> tp_r_bits(RINGSIZEBITS);
          if (id_ == 0) {
            tp_r = tp_share_r.secret();
            tp_r_bits = bitDecomposeTwo(tp_r);
          }
          for (int i = 0; i < RINGSIZEBITS; ++i) {
            OfflineBoolEvaluator::randomShareSecret(nP_, id_, rgen_, share_r_bits[i], tp_share_r_bits[i], tp_r_bits[i],
                                                    b_rand_sh_sec, b_idx_rand_sh_sec);
          }
          // preproc for multk gate 
          auto multk_circ = common::utils::Circuit<BoolRing>::generateMultK().orderGatesByLevel();
          std::vector<preprocg_ptr_t<BoolRing>> multk_gates(multk_circ.num_gates);
          for (const auto& multk_level : multk_circ.gates_by_level) {
            for (auto& multk_gate : multk_level) {
              switch (multk_gate->type) {
                case common::utils::GateType::kInp:{
                  auto pregate = std::make_unique<PreprocInput<BoolRing>>();
                  pregate->pid = 0;
                  multk_gates[multk_gate->out] = std::move(pregate);
                  break;
                }

                case common::utils::GateType::kMul4:{
                  AddShare<BoolRing> share_a; // Holds one share of a random value a
                  TPShare<BoolRing> tp_share_a; // Holds all the shares of a random value a
                  AddShare<BoolRing> share_b; // Holds one of a random value b
                  TPShare<BoolRing> tp_share_b; // Holds all the shares of a random value b
                  AddShare<BoolRing> share_c; // Holds one share of a random value c
                  TPShare<BoolRing> tp_share_c; // Holds all the shares of a random value c
                  AddShare<BoolRing> share_d; // Holds one share of a random value d
                  TPShare<BoolRing> tp_share_d; // Holds all the shares of a random value d
                  AddShare<BoolRing> share_ab; // Holds one share of a*b
                  TPShare<BoolRing> tp_share_ab; // Holds all the shares of a*b
                  AddShare<BoolRing> share_ac; // Holds one share of a*c
                  TPShare<BoolRing> tp_share_ac; // Holds all the shares of a*c
                  AddShare<BoolRing> share_ad; // Holds one share of a*d
                  TPShare<BoolRing> tp_share_ad; // Holds all the shares of a*d
                  AddShare<BoolRing> share_bc; // Holds one share of b*c
                  TPShare<BoolRing> tp_share_bc; // Holds all the shares of b*c
                  AddShare<BoolRing> share_bd; // Holds one share of b*d
                  TPShare<BoolRing> tp_share_bd; // Holds all the shares of b*d
                  AddShare<BoolRing> share_cd; // Holds one share of c*d
                  TPShare<BoolRing> tp_share_cd; // Holds all the shares of c*d
                  AddShare<BoolRing> share_abc; // Holds one share of a*b*c
                  TPShare<BoolRing> tp_share_abc; // Holds all the shares of a*b*c
                  AddShare<BoolRing> share_abd; // Holds one share of a*b*d
                  TPShare<BoolRing> tp_share_abd; // Holds all the shares of a*b*d
                  AddShare<BoolRing> share_acd; // Holds one share of a*c*d
                  TPShare<BoolRing> tp_share_acd; // Holds all the shares of a*c*d
                  AddShare<BoolRing> share_bcd; // Holds one share of b*c*d
                  TPShare<BoolRing> tp_share_bcd; // Holds all the shares of b*c*d
                  AddShare<BoolRing> share_abcd; // Holds one share of a*b*c*d
                  TPShare<BoolRing> tp_share_abcd; // Holds all the shares of a*b*c*d
                  OfflineBoolEvaluator::randomShare(nP_, id_, rgen_, share_a, tp_share_a);
                  OfflineBoolEvaluator::randomShare(nP_, id_, rgen_, share_b, tp_share_b);
                  OfflineBoolEvaluator::randomShare(nP_, id_, rgen_, share_c, tp_share_c);
                  OfflineBoolEvaluator::randomShare(nP_, id_, rgen_, share_d, tp_share_d);
                  BoolRing tp_ab, tp_ac, tp_ad, tp_bc, tp_bd, tp_cd, tp_abc, tp_abd, tp_acd, tp_bcd, tp_abcd;
                  if (id_ == 0) {
                    tp_ab = tp_share_a.secret() * tp_share_b.secret();
                    tp_ac = tp_share_a.secret() * tp_share_c.secret();
                    tp_ad = tp_share_a.secret() * tp_share_d.secret();
                    tp_bc = tp_share_b.secret() * tp_share_c.secret();
                    tp_bd = tp_share_b.secret() * tp_share_d.secret();
                    tp_cd = tp_share_c.secret() * tp_share_d.secret();
                    tp_abc = tp_share_a.secret() * tp_share_b.secret() * tp_share_c.secret();
                    tp_abd = tp_share_a.secret() * tp_share_b.secret() * tp_share_d.secret();
                    tp_acd = tp_share_a.secret() * tp_share_c.secret() * tp_share_d.secret();
                    tp_bcd = tp_share_b.secret() * tp_share_c.secret() * tp_share_d.secret();
                    tp_abcd = tp_share_a.secret() * tp_share_b.secret() * tp_share_c.secret() * tp_share_d.secret();
                  }
                  OfflineBoolEvaluator::randomShareSecret(nP_, id_, rgen_, share_ab, tp_share_ab, tp_ab, b_rand_sh_sec, b_idx_rand_sh_sec);
                  OfflineBoolEvaluator::randomShareSecret(nP_, id_, rgen_, share_ac, tp_share_ac, tp_ac, b_rand_sh_sec, b_idx_rand_sh_sec);
                  OfflineBoolEvaluator::randomShareSecret(nP_, id_, rgen_, share_ad, tp_share_ad, tp_ad, b_rand_sh_sec, b_idx_rand_sh_sec);
                  OfflineBoolEvaluator::randomShareSecret(nP_, id_, rgen_, share_bc, tp_share_bc, tp_bc, b_rand_sh_sec, b_idx_rand_sh_sec);
                  OfflineBoolEvaluator::randomShareSecret(nP_, id_, rgen_, share_bd, tp_share_bd, tp_bd, b_rand_sh_sec, b_idx_rand_sh_sec);
                  OfflineBoolEvaluator::randomShareSecret(nP_, id_, rgen_, share_cd, tp_share_cd, tp_cd, b_rand_sh_sec, b_idx_rand_sh_sec);
                  OfflineBoolEvaluator::randomShareSecret(nP_, id_, rgen_, share_abc, tp_share_abc, tp_abc, b_rand_sh_sec, b_idx_rand_sh_sec);
                  OfflineBoolEvaluator::randomShareSecret(nP_, id_, rgen_, share_abd, tp_share_abd, tp_abd, b_rand_sh_sec, b_idx_rand_sh_sec);
                  OfflineBoolEvaluator::randomShareSecret(nP_, id_, rgen_, share_acd, tp_share_acd, tp_acd, b_rand_sh_sec, b_idx_rand_sh_sec);
                  OfflineBoolEvaluator::randomShareSecret(nP_, id_, rgen_, share_bcd, tp_share_bcd, tp_bcd, b_rand_sh_sec, b_idx_rand_sh_sec);
                  OfflineBoolEvaluator::randomShareSecret(nP_, id_, rgen_, share_abcd, tp_share_abcd, tp_abcd,
                                                          b_rand_sh_sec, b_idx_rand_sh_sec);
                  multk_gates[multk_gate->out] =
                      std::move(std::make_unique<PreprocMult4Gate<BoolRing>>(share_a, tp_share_a, share_b, tp_share_b, share_c, tp_share_c,
                                                                             share_d, tp_share_d, share_ab, tp_share_ab, share_ac, tp_share_ac,
                                                                             share_ad, tp_share_ad, share_bc, tp_share_bc, share_bd,
                                                                             tp_share_bd, share_cd, tp_share_cd, share_abc, tp_share_abc,
                                                                             share_abd, tp_share_abd, share_acd, tp_share_acd, share_bcd,
                                                                             tp_share_bcd, share_abcd, tp_share_abcd));
                  break;
                }
              }
            }
          }
          // // The above method gives boolean output(sharing)
          // // this method expects Field type values and output is also field type
          // // perform Bit2A
          // Ring arith_b;
          // AddShare<Ring> mask_b;
          // TPShare<Ring> tpmask_b;
          // if(id_ == 0) { 
          //   auto wout = multk_circ.outputs[0];
          //   BoolRing bitb;
          //   bitb = multk_gates[wout]->tpmask.secret();
          //   if(bitb == 1) {arith_b = 1; }
          //   else { arith_b = 0;}
          // }
          // randomShareSecret(nP_, id_, rgen_, *network_, 
          //                       mask_b, tpmask_b, arith_b, key, keySh, rand_sh_sec, idx_rand_sh_sec);
          preproc_.gates[gate->out] =
              std::make_unique<PreprocEqzGate<Ring>>(share_r, tp_share_r, share_r_bits, tp_share_r_bits, std::move(multk_gates));
          break;
        }

        case common::utils::GateType::kShuffle: {
          std::vector<AddShare<Ring>> a(vec_size); // Randomly sampled vector
          std::vector<TPShare<Ring>> tp_a(vec_size); // Randomly sampled vector
          std::vector<AddShare<Ring>> b(vec_size); // Randomly sampled vector
          std::vector<TPShare<Ring>> tp_b(vec_size); // Randomly sampled vector
          std::vector<AddShare<Ring>> c(vec_size); // Randomly sampled vector
          std::vector<TPShare<Ring>> tp_c(vec_size); // Randomly sampled vector
          std::vector<AddShare<Ring>> delta(vec_size); // Delta vector only held by the last party. Dummy values for the other parties
          std::vector<int> pi(vec_size); // Randomly sampled permutation using HP
          std::vector<std::vector<int>> tp_pi_all(nP_); // Randomly sampled permutations of all parties using HP
          std::vector<int> pi_common(vec_size); // Common random permutation held by all parties except HP. HP holds dummy values
          for (int i = 0; i < vec_size; i++) {
            randomShare(nP_, id_, rgen_, a[i], tp_a[i]);
            randomShare(nP_, id_, rgen_, b[i], tp_b[i]);
            randomShare(nP_, id_, rgen_, c[i], tp_c[i]);
          }
          randomPermutation(nP_, id_, rgen_, pi, tp_pi_all, vec_size);
          if (id_ > 0) { randomPermutation(nP_, id_, rgen_, pi_common, tp_pi_all, vec_size); }
          generateShuffleDeltaVector(nP_, id_, rgen_, delta, tp_a, tp_b, tp_c, tp_pi_all, vec_size, rand_sh_sec, idx_rand_sh_sec);
          preproc_.gates[gate->out] =
              std::move(std::make_unique<PreprocShuffleGate<Ring>>(a, tp_a, b, tp_b, c, tp_c, delta, pi, tp_pi_all, pi_common));
          break;
        }

        case common::utils::GateType::kPermAndSh: {
          std::vector<AddShare<Ring>> a(vec_size); // Randomly sampled vector
          std::vector<TPShare<Ring>> tp_a(vec_size); // Randomly sampled vector
          std::vector<AddShare<Ring>> b(vec_size); // Randomly sampled vector
          std::vector<TPShare<Ring>> tp_b(vec_size); // Randomly sampled vector
          std::vector<AddShare<Ring>> delta(vec_size); // Delta vector only held by the gate owner party. Dummy values for the other parties
          std::vector<int> pi(vec_size); // Randomly sampled permutation using HP
          std::vector<std::vector<int>> tp_pi_all(nP_); // Randomly sampled permutation of gate owner party using HP.
          std::vector<int> pi_common(vec_size); // Common random permutation held by all parties except HP. HP holds dummy values
          for (int i = 0; i < vec_size; i++) {
            randomShare(nP_, id_, rgen_, a[i], tp_a[i]);
            randomShare(nP_, id_, rgen_, b[i], tp_b[i]);
          }
          randomPermutation(nP_, id_, rgen_, pi, tp_pi_all, vec_size);
          if (id_ != 0) { randomPermutation(nP_, id_, rgen_, pi_common, tp_pi_all, vec_size); }
          generatePermAndShDeltaVector(nP_, id_, rgen_, gate->owner, delta, tp_a, tp_b,
                                       tp_pi_all[gate->owner - 1], vec_size, delta_sh[gate->owner - 1], idx_delta_sh);
          preproc_.gates[gate->out] =
              std::move(std::make_unique<PreprocPermAndShGate<Ring>>(a, tp_a, b, tp_b, delta, pi, tp_pi_all, pi_common));
          break;
        }

        case common::utils::GateType::kAmortzdPnS: {
          std::vector<AddShare<Ring>> a(vec_size); // Randomly sampled vector
          std::vector<TPShare<Ring>> tp_a(vec_size); // Randomly sampled vector
          std::vector<AddShare<Ring>> b(vec_size); // Randomly sampled vector
          std::vector<TPShare<Ring>> tp_b(vec_size); // Randomly sampled vector
          std::vector<AddShare<Ring>> delta(vec_size); // Delta vector only held by all parties for their respective permutation
          std::vector<int> pi(vec_size); // TODO: This should come as input from the party and then secret shared
          std::vector<std::vector<int>> tp_pi_all(nP_); // Randomly sampled permutations of all parties using HP
          std::vector<int> pi_common(vec_size); // Common random permutation held by all parties except HP. HP holds dummy values
          for (int i = 0; i < vec_size; i++) {
            randomShare(nP_, id_, rgen_, a[i], tp_a[i]);
            randomShare(nP_, id_, rgen_, b[i], tp_b[i]);
          }
          randomPermutation(nP_, id_, rgen_, pi, tp_pi_all, vec_size);
          if (id_ != 0) { randomPermutation(nP_, id_, rgen_, pi_common, tp_pi_all, vec_size); }
          for (int pid = 1; pid <= nP_; ++pid) {
            generatePermAndShDeltaVector(nP_, id_, rgen_, pid, delta, tp_a, tp_b,
                                         tp_pi_all[pid - 1], vec_size, delta_sh[pid - 1], idx_delta_sh);
          }
          preproc_.gates[gate->out] =
              std::move(std::make_unique<PreprocAmortzdPnSGate<Ring>>(a, tp_a, b, tp_b, delta, pi, tp_pi_all, pi_common));
          break;
        }

        default: {
          break;
        }
      }
    }
  }
}


void OfflineEvaluator::setWireMasks(const std::unordered_map<common::utils::wire_t, int>& input_pid_map, size_t& vec_size) {
  std::vector<Ring> rand_sh_sec;
  std::vector<BoolRing> b_rand_sh_sec;
  std::vector<std::vector<Ring>> delta_sh(nP_, std::vector<Ring>());

  if (id_ == 0) {
    setWireMasksParty(input_pid_map, rand_sh_sec, b_rand_sh_sec, delta_sh, vec_size);

    for (int pid = 1; pid < nP_; ++pid) {
      size_t delta_sh_num = delta_sh[pid - 1].size();
      network_->send(pid, &delta_sh_num, sizeof(size_t));
      network_->send(pid, delta_sh[pid - 1].data(), delta_sh_num * sizeof(size_t));
    }

    size_t rand_sh_sec_num = rand_sh_sec.size();
    size_t b_rand_sh_sec_num = b_rand_sh_sec.size();
    size_t delta_sh_last_num = delta_sh[nP_ - 1].size();
    size_t arith_comm = rand_sh_sec_num;
    size_t bool_comm = b_rand_sh_sec_num;
    std::vector<size_t> lengths(5);
    lengths[0] = arith_comm;
    lengths[1] = rand_sh_sec_num;
    lengths[2] = bool_comm;
    lengths[3] = b_rand_sh_sec_num;
    lengths[4] = delta_sh_last_num;
    std::cout << lengths[0] << " " << lengths[1] << " " << lengths[2] << " " << lengths[3] << " " << lengths[4] << "\n";

    network_->send(nP_, lengths.data(), sizeof(size_t) * lengths.size());

    std::vector<Ring> offline_arith_comm(arith_comm);
    std::vector<BoolRing> offline_bool_comm(bool_comm);
    for (size_t i = 0; i < rand_sh_sec_num; i++) {
      offline_arith_comm[i] = rand_sh_sec[i];
    }
    for (size_t i = 0; i < b_rand_sh_sec_num; i++) {
      offline_bool_comm[i] = b_rand_sh_sec[i];
    }
    auto net_data = BoolRing::pack(offline_bool_comm.data(), bool_comm);
    network_->send(nP_, offline_arith_comm.data(), sizeof(Ring) * arith_comm);
    network_->send(nP_, net_data.data(), sizeof(uint8_t) * net_data.size());
    network_->send(nP_, delta_sh[nP_ - 1].data(), sizeof(Ring) * delta_sh_last_num);
  } else if (id_ != nP_) {
    size_t delta_sh_num;
    network_->recv(0, &delta_sh_num, sizeof(size_t));
    std::cout << "setWireMasks6 " << delta_sh_num << "\n";
    std::vector<std::vector<Ring>> delta_sh(nP_);
    delta_sh[id_ - 1] = std::vector<Ring>(delta_sh_num);
    network_->recv(0, delta_sh[id_ - 1].data(), delta_sh_num * sizeof(Ring));
    setWireMasksParty(input_pid_map, rand_sh_sec, b_rand_sh_sec, delta_sh, vec_size);
  } else {
    std::vector<size_t> lengths(5);
    network_->recv(0, lengths.data(), sizeof(size_t) * lengths.size());
    size_t arith_comm = lengths[0];
    size_t rand_sh_sec_num = lengths[1];
    size_t bool_comm = lengths[2];
    size_t b_rand_sh_sec_num = lengths[3];
    size_t delta_sh_num = lengths[4];

    auto max_vector_size = 623782648909640;
    std::cout << "setWireMasks5 " << lengths[0] << " " << lengths[1] << " " << lengths[2] << " " << lengths[3] << " " << lengths[4] << "\n";
    if (arith_comm > max_vector_size) {
      std::cout << "Weird Error happening" << std::endl;
      arith_comm = 0;
      rand_sh_sec_num = 0;
      bool_comm = 0;
      b_rand_sh_sec_num = 0;
      delta_sh_num = 0;
    }

    std::vector<Ring> offline_arith_comm(arith_comm);
    network_->recv(0, offline_arith_comm.data(), sizeof(Ring) * arith_comm);

    size_t nbytes = (bool_comm + 7) / 8;
    std::vector<uint8_t> net_data(nbytes);
    network_->recv(0, net_data.data(), nbytes * sizeof(uint8_t));
    std::vector<std::vector<Ring>> delta_sh(nP_);
    delta_sh[id_ - 1] = std::vector<Ring>(delta_sh_num);
    network_->recv(0, delta_sh[id_ - 1].data(), sizeof(Ring) * delta_sh_num);
    auto offline_bool_comm = BoolRing::unpack(net_data.data(), bool_comm);

    rand_sh_sec.resize(rand_sh_sec_num);
    for (int i = 0; i < rand_sh_sec_num; i++) {
      rand_sh_sec[i] = offline_arith_comm[i];
    }
    b_rand_sh_sec.resize(b_rand_sh_sec_num);
    for (int i = 0; i < b_rand_sh_sec_num; i++) {
      b_rand_sh_sec[i] = offline_bool_comm[i];
    }
    setWireMasksParty(input_pid_map, rand_sh_sec, b_rand_sh_sec, delta_sh, vec_size);
  }
}

PreprocCircuit<Ring> OfflineEvaluator::getPreproc() {
  return std::move(preproc_);
}

PreprocCircuit<Ring> OfflineEvaluator::run(const std::unordered_map<common::utils::wire_t, int>& input_pid_map, size_t& vec_size) {
  setWireMasks(input_pid_map, vec_size);
  return std::move(preproc_);
}

OfflineBoolEvaluator::OfflineBoolEvaluator(int nP, int my_id, std::shared_ptr<io::NetIOMP> network,
                                           common::utils::LevelOrderedCircuit circ, int seed)
  : nP_(nP),
    id_(my_id),
    rgen_(my_id, seed),
    network_(std::move(network)),
    circ_(std::move(circ)),
    preproc_() {}

void OfflineBoolEvaluator::randomShare(int nP, int pid, RandGenPool& rgen, AddShare<BoolRing>& share, TPShare<BoolRing>& tpShare) {
  BoolRing val = 0;
  if (pid == 0) {
    share.pushValue(0);
    tpShare.pushValues(0);
    for(int i = 1; i <= nP; i++) {
      uint8_t tmp;
      rgen.pi(i).random_data(&tmp, sizeof(uint8_t));
      val = tmp % 2;
      tpShare.pushValues(val);
    }
  } else {
    uint8_t tmp;
    rgen.p0().random_data(&tmp, sizeof(uint8_t));
    val = tmp % 2;
    share.pushValue(val);
  }
}

void OfflineBoolEvaluator::randomShareSecret(int nP, int pid, RandGenPool& rgen,
                                             AddShare<BoolRing>& share, TPShare<BoolRing>& tpShare, BoolRing secret,
                                             std::vector<BoolRing>& rand_sh_sec, size_t& idx_rand_sh_sec) {
  if (pid == 0) {
    BoolRing val = 0;
    BoolRing valn = 0;
    share.pushValue(0);
    tpShare.pushValues(0);
    for (int i = 1; i < nP; i++) {
      uint8_t tmp;
      rgen.pi(i).random_data(&tmp, sizeof(uint8_t));
      val = tmp % 2; 
      tpShare.pushValues(val);
      valn += val;
    }
    valn = secret - valn;
    tpShare.pushValues(valn);
    rand_sh_sec.push_back(valn);
  } else {
    if (pid != nP) {
      uint8_t tmp;
      rgen.p0().random_data(&tmp, sizeof(uint8_t));
      BoolRing val = tmp % 2;
      share.pushValue(val);
    } else {
      share.pushValue(rand_sh_sec[idx_rand_sh_sec]);
      idx_rand_sh_sec++;
    }
  }
}

// void OfflineBoolEvaluator::setWireMasksParty(const std::unordered_map<common::utils::wire_t, int>& input_pid_map,
//                                              std::vector<BoolRing>& rand_sh_sec) {
//   size_t idx_rand_sh_sec = 0;
//   for (const auto& level : circ_.gates_by_level) {
//     for (const auto& gate : level) {
//       switch (gate->type) {
//         case common::utils::GateType::kInp: {
//           auto pregate = std::make_unique<PreprocInput<BoolRing>>();
//           auto pid = input_pid_map.at(gate->out);
//           pregate->pid = pid;
//           preproc_.gates[gate->out] = std::move(pregate);
//           break;
//         }

//         case common::utils::GateType::kMul: {
//           AddShare<BoolRing> triple_a; // Holds one beaver triple share of a random value a
//           TPShare<BoolRing> tp_triple_a; // Holds all the beaver triple shares of a random value a
//           AddShare<BoolRing> triple_b; // Holds one beaver triple share of a random value b
//           TPShare<BoolRing> tp_triple_b; // Holds all the beaver triple shares of a random value b
//           AddShare<BoolRing> triple_c; // Holds one beaver triple share of c=a*b
//           TPShare<BoolRing> tp_triple_c; // Holds all the beaver triple shares of c=a*b
//           randomShare(nP_, id_, rgen_, triple_a, tp_triple_a);
//           randomShare(nP_, id_, rgen_, triple_b, tp_triple_b);
//           BoolRing tp_prod;
//           if (id_ == 0) { tp_prod = tp_triple_a.secret() * tp_triple_b.secret(); }
//           randomShareSecret(nP_, id_, rgen_, triple_c, tp_triple_c, tp_prod, rand_sh_sec, idx_rand_sh_sec);
//           preproc_.gates[gate->out] =
//               std::move(std::make_unique<PreprocMultGate<BoolRing>>(triple_a, tp_triple_a, triple_b, tp_triple_b, triple_c, tp_triple_c));
//           break;
//         }

//         case common::utils::GateType::kMul3: {
//           AddShare<BoolRing> share_a; // Holds one share of a random value a
//           TPShare<BoolRing> tp_share_a; // Holds all the shares of a random value a
//           AddShare<BoolRing> share_b; // Holds one of a random value b
//           TPShare<BoolRing> tp_share_b; // Holds all the shares of a random value b
//           AddShare<BoolRing> share_c; // Holds one share of a random value c
//           TPShare<BoolRing> tp_share_c; // Holds all the shares of a random value c
//           AddShare<BoolRing> share_ab; // Holds one share of a*b
//           TPShare<BoolRing> tp_share_ab; // Holds all the shares of a*b
//           AddShare<BoolRing> share_bc; // Holds one share of b*c
//           TPShare<BoolRing> tp_share_bc; // Holds all the shares of b*c
//           AddShare<BoolRing> share_ca; // Holds one share of c*a
//           TPShare<BoolRing> tp_share_ca; // Holds all the shares of c*a
//           AddShare<BoolRing> share_abc; // Holds one share of a*b*c
//           TPShare<BoolRing> tp_share_abc; // Holds all the shares of a*b*c
//           randomShare(nP_, id_, rgen_, share_a, tp_share_a);
//           randomShare(nP_, id_, rgen_, share_b, tp_share_b);
//           randomShare(nP_, id_, rgen_, share_c, tp_share_c);
//           BoolRing tp_ab, tp_bc, tp_ca, tp_abc;
//           if (id_ == 0) {
//             tp_ab = tp_share_a.secret() * tp_share_b.secret();
//             tp_bc = tp_share_b.secret() * tp_share_c.secret();
//             tp_ca = tp_share_c.secret() * tp_share_a.secret();
//             tp_abc = tp_share_a.secret() * tp_share_b.secret() * tp_share_c.secret();
//           }
//           randomShareSecret(nP_, id_, rgen_, share_ab, tp_share_ab, tp_ab, rand_sh_sec, idx_rand_sh_sec);
//           randomShareSecret(nP_, id_, rgen_, share_bc, tp_share_bc, tp_bc, rand_sh_sec, idx_rand_sh_sec);
//           randomShareSecret(nP_, id_, rgen_, share_ca, tp_share_ca, tp_ca, rand_sh_sec, idx_rand_sh_sec);
//           randomShareSecret(nP_, id_, rgen_, share_abc, tp_share_abc, tp_abc, rand_sh_sec, idx_rand_sh_sec);
//           preproc_.gates[gate->out] =
//               std::move(std::make_unique<PreprocMult3Gate<BoolRing>>(share_a, tp_share_a, share_b, tp_share_b, share_c, tp_share_c,
//                                                                  share_ab, tp_share_ab, share_bc, tp_share_bc, share_ca, tp_share_ca,
//                                                                  share_abc, tp_share_abc));
//           break;
//         }

//         case common::utils::GateType::kMul4: {
//           AddShare<BoolRing> share_a; // Holds one share of a random value a
//           TPShare<BoolRing> tp_share_a; // Holds all the shares of a random value a
//           AddShare<BoolRing> share_b; // Holds one of a random value b
//           TPShare<BoolRing> tp_share_b; // Holds all the shares of a random value b
//           AddShare<BoolRing> share_c; // Holds one share of a random value c
//           TPShare<BoolRing> tp_share_c; // Holds all the shares of a random value c
//           AddShare<BoolRing> share_d; // Holds one share of a random value d
//           TPShare<BoolRing> tp_share_d; // Holds all the shares of a random value d
//           AddShare<BoolRing> share_ab; // Holds one share of a*b
//           TPShare<BoolRing> tp_share_ab; // Holds all the shares of a*b
//           AddShare<BoolRing> share_ac; // Holds one share of a*c
//           TPShare<BoolRing> tp_share_ac; // Holds all the shares of a*c
//           AddShare<BoolRing> share_ad; // Holds one share of a*d
//           TPShare<BoolRing> tp_share_ad; // Holds all the shares of a*d
//           AddShare<BoolRing> share_bc; // Holds one share of b*c
//           TPShare<BoolRing> tp_share_bc; // Holds all the shares of b*c
//           AddShare<BoolRing> share_bd; // Holds one share of b*d
//           TPShare<BoolRing> tp_share_bd; // Holds all the shares of b*d
//           AddShare<BoolRing> share_cd; // Holds one share of c*d
//           TPShare<BoolRing> tp_share_cd; // Holds all the shares of c*d
//           AddShare<BoolRing> share_abc; // Holds one share of a*b*c
//           TPShare<BoolRing> tp_share_abc; // Holds all the shares of a*b*c
//           AddShare<BoolRing> share_abd; // Holds one share of a*b*d
//           TPShare<BoolRing> tp_share_abd; // Holds all the shares of a*b*d
//           AddShare<BoolRing> share_acd; // Holds one share of a*c*d
//           TPShare<BoolRing> tp_share_acd; // Holds all the shares of a*c*d
//           AddShare<BoolRing> share_bcd; // Holds one share of b*c*d
//           TPShare<BoolRing> tp_share_bcd; // Holds all the shares of b*c*d
//           AddShare<BoolRing> share_abcd; // Holds one share of a*b*c*d
//           TPShare<BoolRing> tp_share_abcd; // Holds all the shares of a*b*c*d
//           randomShare(nP_, id_, rgen_, share_a, tp_share_a);
//           randomShare(nP_, id_, rgen_, share_b, tp_share_b);
//           randomShare(nP_, id_, rgen_, share_c, tp_share_c);
//           randomShare(nP_, id_, rgen_, share_d, tp_share_d);
//           BoolRing tp_ab, tp_ac, tp_ad, tp_bc, tp_bd, tp_cd, tp_abc, tp_abd, tp_acd, tp_bcd, tp_abcd;
//           if (id_ == 0) {
//             tp_ab = tp_share_a.secret() * tp_share_b.secret();
//             tp_ac = tp_share_a.secret() * tp_share_c.secret();
//             tp_ad = tp_share_a.secret() * tp_share_d.secret();
//             tp_bc = tp_share_b.secret() * tp_share_c.secret();
//             tp_bd = tp_share_b.secret() * tp_share_d.secret();
//             tp_cd = tp_share_c.secret() * tp_share_d.secret();
//             tp_abc = tp_share_a.secret() * tp_share_b.secret() * tp_share_c.secret();
//             tp_abd = tp_share_a.secret() * tp_share_b.secret() * tp_share_d.secret();
//             tp_acd = tp_share_a.secret() * tp_share_c.secret() * tp_share_d.secret();
//             tp_bcd = tp_share_b.secret() * tp_share_c.secret() * tp_share_d.secret();
//             tp_abcd = tp_share_a.secret() * tp_share_b.secret() * tp_share_c.secret() * tp_share_d.secret();
//           }
//           randomShareSecret(nP_, id_, rgen_, share_ab, tp_share_ab, tp_ab, rand_sh_sec, idx_rand_sh_sec);
//           randomShareSecret(nP_, id_, rgen_, share_ac, tp_share_ac, tp_ac, rand_sh_sec, idx_rand_sh_sec);
//           randomShareSecret(nP_, id_, rgen_, share_ad, tp_share_ad, tp_ad, rand_sh_sec, idx_rand_sh_sec);
//           randomShareSecret(nP_, id_, rgen_, share_bc, tp_share_bc, tp_bc, rand_sh_sec, idx_rand_sh_sec);
//           randomShareSecret(nP_, id_, rgen_, share_bd, tp_share_bd, tp_bd, rand_sh_sec, idx_rand_sh_sec);
//           randomShareSecret(nP_, id_, rgen_, share_cd, tp_share_cd, tp_cd, rand_sh_sec, idx_rand_sh_sec);
//           randomShareSecret(nP_, id_, rgen_, share_abc, tp_share_abc, tp_abc, rand_sh_sec, idx_rand_sh_sec);
//           randomShareSecret(nP_, id_, rgen_, share_abd, tp_share_abd, tp_abd, rand_sh_sec, idx_rand_sh_sec);
//           randomShareSecret(nP_, id_, rgen_, share_acd, tp_share_acd, tp_acd, rand_sh_sec, idx_rand_sh_sec);
//           randomShareSecret(nP_, id_, rgen_, share_bcd, tp_share_bcd, tp_bcd, rand_sh_sec, idx_rand_sh_sec);
//           randomShareSecret(nP_, id_, rgen_, share_abcd, tp_share_abcd, tp_abcd, rand_sh_sec, idx_rand_sh_sec);
//           preproc_.gates[gate->out] =
//               std::move(std::make_unique<PreprocMult4Gate<BoolRing>>(share_a, tp_share_a, share_b, tp_share_b, share_c, tp_share_c,
//                                                                  share_d, tp_share_d, share_ab, tp_share_ab, share_ac, tp_share_ac,
//                                                                  share_ad, tp_share_ad, share_bc, tp_share_bc, share_bd, tp_share_bd,
//                                                                  share_cd, tp_share_cd, share_abc, tp_share_abc, share_abd, tp_share_abd,
//                                                                  share_acd, tp_share_acd, share_bcd, tp_share_bcd, share_abcd, tp_share_abcd));
//           break;
//         }

//         default: {
//           break;
//         }
//       }
//     }
//   }
// }

// void OfflineBoolEvaluator::setWireMasks(const std::unordered_map<common::utils::wire_t, int>& input_pid_map) {   
//   std::vector<BoolRing> rand_sh_sec;
//   size_t idx_rand_sh_sec;
//   if (id_ != nP_) {
//     setWireMasksParty(input_pid_map, rand_sh_sec);
//     if (id_ == 0) {
//       size_t rand_sh_sec_num = rand_sh_sec.size();
//       size_t total_comm = rand_sh_sec_num;
//       std::vector<size_t> lengths(2);
//       lengths[0] = total_comm;
//       lengths[1] = rand_sh_sec_num;
//       network_->send(nP_, lengths.data(), sizeof(size_t) * lengths.size());
//       std::vector<BoolRing> offline_comm(total_comm);
//       for (size_t i = 0; i < rand_sh_sec_num; i++) {
//         offline_comm[i] = rand_sh_sec[i];
//       }
//       network_->send(nP_, offline_comm.data(), sizeof(BoolRing) * total_comm);
//     }
//   } else {
//     std::vector<size_t> lengths(2);
//     network_->recv(0, lengths.data(), sizeof(size_t) * lengths.size());
//     size_t total_comm = lengths[0];
//     size_t rand_sh_sec_num = lengths[1];
//     std::vector<BoolRing> offline_comm(total_comm);
//     network_->recv(0, offline_comm.data(), sizeof(BoolRing) * total_comm);
//     rand_sh_sec.resize(rand_sh_sec_num);
//     for(int i = 0; i < rand_sh_sec_num; i++) {
//       rand_sh_sec[i] = offline_comm[i];
//     }
//     setWireMasksParty(input_pid_map, rand_sh_sec);
//   }
// }

// PreprocCircuit<BoolRing> OfflineBoolEvaluator::getPreproc() {
//   return std::move(preproc_);
// }

// PreprocCircuit<BoolRing> OfflineBoolEvaluator::run(const std::unordered_map<common::utils::wire_t, int>& input_pid_map) {
//   setWireMasks(input_pid_map);
//   return std::move(preproc_);
// }
};  // namespace asterisk
