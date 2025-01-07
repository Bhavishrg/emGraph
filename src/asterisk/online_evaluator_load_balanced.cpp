#include "online_evaluator.h"

#include <array>

#include "../utils/helpers.h"

namespace asterisk
{
    OnlineEvaluator::OnlineEvaluator(int nP, int id, std::shared_ptr<io::NetIOMP> network,
                                     PreprocCircuit<Ring> preproc,
                                     common::utils::LevelOrderedCircuit circ,
                                     int threads, int seed)
        : nP_(nP),
          id_(id),
          rgen_(id, seed),
          network_(std::move(network)),
          preproc_(std::move(preproc)),
          circ_(std::move(circ)),
          wires_(circ.num_gates)
    {
        tpool_ = std::make_shared<ThreadPool>(threads);
    }

    OnlineEvaluator::OnlineEvaluator(int nP, int id, std::shared_ptr<io::NetIOMP> network,
                                     PreprocCircuit<Ring> preproc,
                                     common::utils::LevelOrderedCircuit circ,
                                     std::shared_ptr<ThreadPool> tpool, int seed)
        : nP_(nP),
          id_(id),
          rgen_(id, seed),
          network_(std::move(network)),
          preproc_(std::move(preproc)),
          circ_(std::move(circ)),
          tpool_(std::move(tpool)),
          wires_(circ.num_gates) {}

    void OnlineEvaluator::setInputs(const std::unordered_map<common::utils::wire_t, Ring> &inputs) {
        // Input gates have depth 0
        for (auto &g : circ_.gates_by_level[0]) {
            if (g->type == common::utils::GateType::kInp) {
                auto *pre_input = static_cast<PreprocInput<Ring> *>(preproc_.gates[g->out].get());
                auto pid = pre_input->pid;
                if (id_ != 0) {
                    if (pid == id_) {
                        Ring accumulated_val = Ring(0);
                        for (size_t i = 1; i <= nP_; i++) {
                            if (i != pid) {
                                Ring rand_sh;
                                rgen_.pi(i).random_data(&rand_sh, sizeof(Ring)); // TODO: PRG should be pairwise common b/w input owner and pid
                                accumulated_val += rand_sh;
                            }
                        }
                        wires_[g->out] = inputs.at(g->out) - accumulated_val;
                    } else {
                        rgen_.pi(id_).random_data(&wires_[g->out], sizeof(Ring)); // TODO: PRG should be pairwise common b/w input owner and pid
                    }
                }
            }
        }
    }

    void OnlineEvaluator::setRandomInputs() {
        // Input gates have depth 0.
        for (auto &g : circ_.gates_by_level[0]) {
            if (g->type == common::utils::GateType::kInp) {
                rgen_.pi(id_).random_data(&wires_[g->out], sizeof(Ring)); // TODO: What key to use for PRG
            }
        }
    }

    void OnlineEvaluator::shuffleEvaluate(std::vector<common::utils::SIMDOGate> &shuffle_gates) {
        if (id_ == 0) { return; }
        std::vector<Ring> z_all;
        std::vector<std::vector<Ring>> z_sum;
        size_t total_comm = 0;

        for (auto &gate : shuffle_gates) {
            auto *pre_shuffle = static_cast<PreprocShuffleGate<Ring> *>(preproc_.gates[gate.out].get());
            size_t vec_size = gate.in.size();
            total_comm += vec_size;
            std::vector<Ring> z(vec_size, 0);
            if (id_ != 1) {
                for (int i = 0; i < vec_size; ++i) {
                    z[i] = gate.in[i] - pre_shuffle->a[i].valueAt();
                }
                z_all.insert(z_all.end(), z.begin(), z.end());
            } else {
                z_sum.push_back(z);
            }
        }

        if (id_ == 1) {
            z_all.reserve(total_comm);
            for (int pid = 2; pid <= nP_; ++pid) {
                network_->recv(pid, z_all.data(), total_comm * sizeof(Ring));
                size_t idx_vec = 0;
                for (int idx_gate = 0; idx_gate < shuffle_gates.size(); ++idx_gate) {
                    size_t vec_size = shuffle_gates[idx_gate].in.size();
                    std::vector<Ring> z(z_all.begin() + idx_vec, z_all.begin() + idx_vec + vec_size);
                    for (int i = 0; i < vec_size; ++i) {
                        z_sum[idx_gate][i] += z[i];
                    }
                    idx_vec += vec_size;
                }
            }

            z_all.clear();
            z_all.reserve(total_comm);
            for (int idx_gate = 0; idx_gate < shuffle_gates.size(); ++idx_gate) {
                auto *pre_shuffle = static_cast<PreprocShuffleGate<Ring> *>(preproc_.gates[shuffle_gates[idx_gate].out].get());
                size_t vec_size = shuffle_gates[idx_gate].in.size();
                std::vector<Ring> z(vec_size);
                for (int i = 0; i < vec_size; ++i) {
                    z[i] = z_sum[idx_gate][pre_shuffle->pi[i]] + shuffle_gates[idx_gate].in[pre_shuffle->pi[i]] - pre_shuffle->c[i].valueAt();
                    wires_[shuffle_gates[idx_gate].outs[i]] = pre_shuffle->b[i].valueAt();
                }
                z_all.insert(z_all.end(), z.begin(), z.end());
            }
        } else {
            network_->send(1, z_all.data(), total_comm * sizeof(Ring));

            network_->recv(id_ - 1, z_all.data(), total_comm * sizeof(Ring));
            size_t idx_vec = 0;
            for (int idx_gate = 0; idx_gate < shuffle_gates.size(); ++idx_gate) {
                auto *pre_shuffle = static_cast<PreprocShuffleGate<Ring> *>(preproc_.gates[shuffle_gates[idx_gate].out].get());
                size_t vec_size = shuffle_gates[idx_gate].in.size();
                std::vector<Ring> z(z_all.begin() + idx_vec, z_all.begin() + idx_vec + vec_size);
                std::vector<Ring> z_send(vec_size);
                for (int i = 0; i < vec_size; ++i) {
                    if (id_ != nP_) {
                        z_send[i] = z[pre_shuffle->pi[i]] - pre_shuffle->c[i].valueAt();
                        wires_[shuffle_gates[idx_gate].outs[i]] = pre_shuffle->b[i].valueAt();
                    } else {
                        z_send[i] = z[pre_shuffle->pi[i]] + pre_shuffle->delta[i].valueAt();
                        wires_[shuffle_gates[idx_gate].outs[i]] = z_send[i];
                    }
                }
                z_all.insert(z_all.begin() + idx_vec, z_send.begin(), z_send.end());
                idx_vec += vec_size;
            }
            if (id_ != nP_) {
                network_->send(id_ + 1, z_all.data(), total_comm * sizeof(Ring));
            }
        }
    }

    void OnlineEvaluator::permAndShEvaluate(std::vector<common::utils::SIMDOGate> &permAndSh_gates) {
        if (id_ == 0) { return; }
        for (auto &gate : permAndSh_gates) {
            auto *pre_permAndSh = static_cast<PreprocPermAndShGate<Ring> *>(preproc_.gates[gate.out].get());
            size_t vec_size = gate.in.size();
            std::vector<Ring> z(vec_size, 0);
            if (id_ != gate.owner) {
                for (int i = 0; i < vec_size; ++i) {
                    z[i] = gate.in[i] - pre_permAndSh->a[i].valueAt();
                    wires_[gate.outs[i]] = pre_permAndSh->b[i].valueAt();
                }
                network_->send(gate.owner, z.data(), z.size() * sizeof(Ring));
            } else {
                for (int pid = 1; pid <= nP_; ++pid) {
                    std::vector<Ring> z_recv(vec_size);
                    if (pid != gate.owner) {
                        network_->recv(pid, z_recv.data(), z_recv.size() * sizeof(Ring));
                        for (int i = 0; i < vec_size; ++i) {
                            z[i] += z_recv[i];
                        }
                    } else {
                        for (int i = 0; i < vec_size; ++i) {
                            z[i] += gate.in[i];
                        }
                    }
                }
                for (int i = 0; i < vec_size; ++i) {
                    wires_[gate.outs[i]] = z[pre_permAndSh->pi[i]] + pre_permAndSh->delta[i].valueAt();
                }
            }
        }
    }

    void OnlineEvaluator::amortzdPnSEvaluate(std::vector<common::utils::SIMDMOGate> &amortzdPnS_gates) {
        if (id_ == 0) { return; }
        int pKing = 1; // Designated king party
        std::vector<std::vector<Ring>> z_sum;
        size_t total_comm = 0;

        for (auto &gate : amortzdPnS_gates) {
            auto *pre_amortzdPnS = static_cast<PreprocAmortzdPnSGate<Ring> *>(preproc_.gates[gate.out].get());
            size_t vec_size = gate.in.size();

            std::vector<Ring> z(vec_size);
            for (int i = 0; i < vec_size; ++i) {
                z[i] = gate.in[i] - pre_amortzdPnS->a[i].valueAt();
            }

            std::vector<Ring> z_recon(vec_size, 0);
            if (id_ != pKing) {
                network_->send(pKing, z.data(), z.size() * sizeof(Ring));
                network_->recv(pKing, z_recon.data(), z_recon.size() * sizeof(Ring));
            } else {
                z_sum.reserve(nP_);
                for (int pid = 1; pid <= nP_; ++pid) {
                    std::vector<Ring> z_recv(vec_size);
                    if (pid != pKing) {
                        network_->recv(pid, z_recv.data(), z_recv.size() * sizeof(Ring));
                        z_sum.push_back(z_recv);
                    } else {
                        z_sum.push_back(z);
                    }
                }
                for (int i = 0; i < vec_size; ++i) {
                    for (int pid = 0; pid < nP_; ++pid) {
                        z_recon[i] += z_sum[pid][i];
                    }
                }
                for (int pid = 1; pid <= nP_; ++pid) {
                    if (pid != pKing) {
                        network_->send(pid, z_recon.data(), z_recon.size() * sizeof(Ring));
                    }
                }
            }

            for (int pid = 0; pid < nP_; ++pid) {
                for (int i = 0; i < vec_size; ++i) {
                    if (pid == id_) {
                        wires_[gate.multi_outs[pid][i]] = z_recon[pre_amortzdPnS->pi[i]] + pre_amortzdPnS->delta[i].valueAt();
                    } else {
                        wires_[gate.multi_outs[pid][i]] = pre_amortzdPnS->b[i].valueAt();
                    }
                }
            }
        }
    }

    void OnlineEvaluator::evaluateGatesAtDepthPartySend(size_t depth, std::vector<Ring> &mult_vals) {
        for (auto &gate : circ_.gates_by_level[depth]) {
            switch (gate->type) {
                case common::utils::GateType::kMul: {
                    auto *g = static_cast<common::utils::FIn2Gate *>(gate.get());
                    if (id_ != 0) {
                        auto *pre_out = static_cast<PreprocMultGate<Ring> *>(preproc_.gates[g->out].get());
                        auto u = pre_out->triple_a.valueAt() - wires_[g->in1];
                        auto v = pre_out->triple_b.valueAt() - wires_[g->in2];
                        mult_vals.push_back(u);
                        mult_vals.push_back(v);
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }

    void OnlineEvaluator::evaluateGatesAtDepthPartyRecv(size_t depth, std::vector<Ring> &mult_vals) {
        size_t idx_mult = 0;
        for (auto &gate : circ_.gates_by_level[depth]) {
            switch (gate->type) {
                case common::utils::GateType::kAdd: {
                    auto *g = static_cast<common::utils::FIn2Gate *>(gate.get());
                    if (id_ != 0)
                        wires_[g->out] = wires_[g->in1] + wires_[g->in2];
                    break;
                }

                case common::utils::GateType::kSub: {
                    auto *g = static_cast<common::utils::FIn2Gate *>(gate.get());
                    if (id_ != 0)
                        wires_[g->out] = wires_[g->in1] - wires_[g->in2];
                    break;
                }

                case common::utils::GateType::kConstAdd: {
                    auto *g = static_cast<common::utils::ConstOpGate<Ring> *>(gate.get());
                    wires_[g->out] = wires_[g->in] + g->cval;
                    break;
                }

                case common::utils::GateType::kConstMul: {
                    auto *g = static_cast<common::utils::ConstOpGate<Ring> *>(gate.get());
                    if (id_ != 0)
                        wires_[g->out] = wires_[g->in] * g->cval;
                    break;
                }

                case common::utils::GateType::kMul: {
                    auto *g = static_cast<common::utils::FIn2Gate *>(gate.get());
                    if (id_ != 0) {
                        auto *pre_out = static_cast<PreprocMultGate<Ring> *>(preproc_.gates[g->out].get());
                        Ring u = Ring(0);
                        Ring v = Ring(0);
                        Ring a = pre_out->triple_a.valueAt();
                        Ring b = pre_out->triple_b.valueAt();
                        Ring c = pre_out->triple_c.valueAt();
                        for (int i = 1; i <= nP_; ++i) {
                            u += mult_vals[idx_mult++];
                            v += mult_vals[idx_mult++];
                        }
                        wires_[g->out] = u * v + u * b + v * a + c;
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }

    void OnlineEvaluator::evaluateGatesAtDepth(size_t depth) {
        size_t mult_num = 0;
        size_t shuffle_num = 0;
        size_t permAndSh_num = 0;
        size_t amortzdPnS_num = 0;

        std::vector<Ring> mult_vals;
        std::vector<common::utils::SIMDOGate> shuffle_gates;
        std::vector<common::utils::SIMDOGate> permAndSh_gates;
        std::vector<common::utils::SIMDMOGate> amortzdPnS_gates;

        for (auto &gate : circ_.gates_by_level[depth]) {
            switch (gate->type) {
                case common::utils::GateType::kMul: {
                    mult_num++;
                    break;
                }

                case common::utils::GateType::kShuffle: {
                    auto *g = static_cast<common::utils::SIMDOGate *>(gate.get());
                    shuffle_gates.push_back(*g);
                    shuffle_num++;
                    break;
                }

                case common::utils::GateType::kPermAndSh: {
                    auto *g = static_cast<common::utils::SIMDOGate *>(gate.get());
                    permAndSh_gates.push_back(*g);
                    permAndSh_num++;
                    break;
                }

                case common::utils::GateType::kAmortzdPnS: {
                    auto *g = static_cast<common::utils::SIMDMOGate *>(gate.get());
                    amortzdPnS_gates.push_back(*g);
                    amortzdPnS_num++;
                    break;
                }
            }
        }

        if (shuffle_num > 0) {
            shuffleEvaluate(shuffle_gates);
        }

        if (permAndSh_num > 0) {
            permAndShEvaluate(permAndSh_gates);
        }

        if (amortzdPnS_num > 0) {
            amortzdPnSEvaluate(amortzdPnS_gates);
        }

        if (id_ != 0) {
            evaluateGatesAtDepthPartySend(depth, mult_vals);
            size_t total_comm_send = mult_vals.size();
            size_t total_comm_recv = nP_ * mult_vals.size();
            std::vector<Ring> online_comm_send;
            online_comm_send.reserve(total_comm_send);
            std::vector<Ring> online_comm_recv;
            online_comm_recv.reserve(total_comm_recv);
            std::vector<Ring> online_comm_recv_party;
            online_comm_recv_party.reserve(total_comm_send);
            online_comm_send.insert(online_comm_send.begin(), mult_vals.begin(), mult_vals.end());

            for (int pid = 1; pid <= nP_; ++pid) {
                if (pid != id_) {
                    network_->send(pid, online_comm_send.data(), sizeof(Ring) * total_comm_send);
                }
            }

            for (int pid = 1; pid <= nP_; ++pid) {
                if (pid != id_) {
                    network_->recv(pid, online_comm_recv_party.data(), sizeof(Ring) * total_comm_send);
                } else {
                    online_comm_recv_party.insert(online_comm_recv_party.begin(), online_comm_send.begin(), online_comm_send.end());
                }
                online_comm_recv.insert(online_comm_recv.end(), online_comm_recv_party.begin(), online_comm_recv_party.end());
            }

            size_t mult_all_recv = online_comm_recv.size();
            std::vector<Ring> mult_all(mult_all_recv);
            for (int i = 0, j = 0, pid = 0; i < mult_all_recv; ++i) {
                mult_all[i] = online_comm_recv[2 * (pid * mult_num + j)];
                mult_all[++i] = online_comm_recv[2 * (pid * mult_num + j) + 1];
                j += (pid + 1) / nP_;
                pid = (pid + 1) % nP_;
            }
            evaluateGatesAtDepthPartyRecv(depth, mult_all);
        }
    }

    std::vector<Ring> OnlineEvaluator::getOutputs() {
        std::vector<Ring> outvals(circ_.outputs.size());
        if (circ_.outputs.empty()) {
            return outvals;
        }
        if (id_ != 0) {
            std::vector<std::vector<Ring>> output_shares(nP_, std::vector<Ring>(circ_.outputs.size()));
            for (size_t i = 0; i < circ_.outputs.size(); ++i) {
                auto wout = circ_.outputs[i];
                output_shares[id_][i] = wires_[wout];
            }
            for (int pid = 1; pid <= nP_; ++pid) {
                if (pid != id_) {
                    network_->send(pid, output_shares[id_].data(), output_shares[id_].size() * sizeof(Ring));
                }
            }
            for (int pid = 1; pid <= nP_; ++pid) {
                if (pid != id_) {
                    network_->recv(pid, output_shares[pid].data(), output_shares[pid].size() * sizeof(Ring));
                }
            }
            for (size_t i = 0; i < circ_.outputs.size(); ++i) {
                Ring outmask = Ring(0);
                for (int pid = 1; pid <= nP_; ++pid) {
                    outmask += output_shares[pid][i];
                }
                outvals[i] = outmask;
            }
        }
        return outvals;
    }

    Ring OnlineEvaluator::reconstruct(AddShare<Ring> &shares) {
        Ring reconstructed_value = Ring(0);
        if (id_ != 0) {
            for (size_t i = 1; i <= nP_; ++i) {
                if (i != id_) {
                    network_->send(i, &shares.valueAt(), sizeof(Ring));
                }
            }
            for (size_t i = 1; i <= nP_; ++i) {
                Ring share_val = Ring(0);
                if (i != id_) {
                    network_->recv(i, &share_val, sizeof(Ring));
                }
                reconstructed_value += share_val;
            }
        }
        return reconstructed_value;
    }

    std::vector<Ring> OnlineEvaluator::evaluateCircuit(const std::unordered_map<common::utils::wire_t, Ring> &inputs) {
        setInputs(inputs);
        for (size_t i = 0; i < circ_.gates_by_level.size(); ++i) {
            evaluateGatesAtDepth(i);
        }
        return getOutputs();
    }
}; // namespace asterisk
