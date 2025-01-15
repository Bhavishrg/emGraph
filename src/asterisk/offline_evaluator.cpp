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
  } else if (pid > 0) {
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
  } else if (pid > 0) {
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
                                         std::vector<Ring>& rand_sh_party, std::vector<BoolRing>& b_rand_sh_party,
                                         std::vector<std::vector<Ring>>& delta_sh, size_t& vec_size) {
  size_t idx_rand_sh_sec = 0;
  size_t idx_delta_sh = 0;

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
          preproc_.gates[gate->out] = std::move(std::make_unique<PreprocShuffleGate<Ring>>(a, tp_a, b, tp_b, c, tp_c, delta, pi, tp_pi_all, pi_common));
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
          std::vector<int> pi(vec_size); // Randomly sampled permutation using HP
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
  std::vector<Ring> rand_sh_party;
  std::vector<BoolRing> b_rand_sh_party;
  std::vector<std::vector<Ring>> delta_sh(nP_, std::vector<Ring>());

  if (id_ == 0) {
    setWireMasksParty(input_pid_map, rand_sh_sec, b_rand_sh_sec, rand_sh_party, b_rand_sh_party, delta_sh, vec_size);

    for (int pid = 1; pid < nP_; ++pid) {
      size_t delta_sh_num = delta_sh[pid - 1].size();
      network_->send(pid, &delta_sh_num, sizeof(size_t));
      network_->send(pid, delta_sh[pid - 1].data(), delta_sh_num * sizeof(size_t));
    }

    size_t rand_sh_sec_num = rand_sh_sec.size();
    size_t b_rand_sh_sec_num = b_rand_sh_sec.size();
    size_t rand_sh_party_num = rand_sh_party.size();
    size_t b_rand_sh_party_num = b_rand_sh_party.size();
    size_t delta_sh_last_num = delta_sh[nP_ - 1].size();
    size_t arith_comm = rand_sh_sec_num + rand_sh_party_num;
    size_t bool_comm = b_rand_sh_sec_num + b_rand_sh_party_num;
    std::vector<size_t> lengths(7);
    lengths[0] = arith_comm;
    lengths[1] = rand_sh_sec_num;
    lengths[2] = rand_sh_party_num;
    lengths[3] = bool_comm;
    lengths[4] = b_rand_sh_sec_num;
    lengths[5] = b_rand_sh_party_num;
    lengths[6] = delta_sh_last_num;
    std::cout << lengths[0] << " " << lengths[1] << " " << lengths[2] << " " << lengths[3] << " " << lengths[4] << " " << lengths[5] << " " << lengths[6] << "\n";

    network_->send(nP_, lengths.data(), sizeof(size_t) * lengths.size());

    std::vector<Ring> offline_arith_comm(arith_comm);
    std::vector<BoolRing> offline_bool_comm(bool_comm);
    for (size_t i = 0; i < rand_sh_sec_num; i++) {
      offline_arith_comm[i] = rand_sh_sec[i];
    }
    for (size_t i = 0; i < rand_sh_party_num; i++) {
      offline_arith_comm[rand_sh_sec_num + i] = rand_sh_party[i];
    }
    for (size_t i = 0; i < b_rand_sh_sec_num; i++) {
      offline_bool_comm[i] = b_rand_sh_sec[i];
    }
    for (size_t i = 0; i < b_rand_sh_party_num; i++) {
      offline_bool_comm[b_rand_sh_sec_num + i] = b_rand_sh_party[i];
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
    setWireMasksParty(input_pid_map, rand_sh_sec, b_rand_sh_sec, rand_sh_party, b_rand_sh_party, delta_sh, vec_size);
  } else {
    std::vector<size_t> lengths(7);
    network_->recv(0, lengths.data(), sizeof(size_t) * lengths.size());
    size_t arith_comm = lengths[0];
    size_t rand_sh_sec_num = lengths[1];
    size_t rand_sh_party_num = lengths[2];
    size_t bool_comm = lengths[3];
    size_t b_rand_sh_sec_num = lengths[4];
    size_t b_rand_sh_party_num = lengths[5];
    size_t delta_sh_num = lengths[6];

    auto max_vector_size = 623782648909640;
    std::cout << "setWireMasks5 " << lengths[0] << " " << lengths[1] << " " << lengths[2] << " " << lengths[3] << " " << lengths[4] << " " << lengths[5] << " " << lengths[6] << "\n";
    if (arith_comm > max_vector_size) {
      std::cout << "Weird Error happening" << std::endl;
      arith_comm = 0;
      rand_sh_sec_num = 0;
      rand_sh_party_num = 0;
      bool_comm = 0;
      b_rand_sh_sec_num = 0;
      b_rand_sh_party_num = 0;
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
    rand_sh_party.resize(rand_sh_party_num);
    for (int i = 0; i < rand_sh_party_num; i++) {
      rand_sh_party[i] = offline_arith_comm[rand_sh_sec_num + i];
    }
    b_rand_sh_sec.resize(b_rand_sh_sec_num);
    for (int i = 0; i < b_rand_sh_sec_num; i++) {
      b_rand_sh_sec[i] = offline_bool_comm[i];
    }
    b_rand_sh_party.resize(b_rand_sh_party_num);
    for (int i = 0; i < b_rand_sh_party_num; i++) {
      b_rand_sh_party[i] = offline_bool_comm[b_rand_sh_sec_num + i];
    }
    setWireMasksParty(input_pid_map, rand_sh_sec, b_rand_sh_sec, rand_sh_party, b_rand_sh_party, delta_sh, vec_size);
  }
}

PreprocCircuit<Ring> OfflineEvaluator::getPreproc() {
  return std::move(preproc_);
}

PreprocCircuit<Ring> OfflineEvaluator::run(const std::unordered_map<common::utils::wire_t, int>& input_pid_map, size_t& vec_size) {
  setWireMasks(input_pid_map, vec_size);
  return std::move(preproc_);
}
};  // namespace asterisk
