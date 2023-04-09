#include"online_evaluator.h"

#include <array>

#include"dirigent/helpers.h"

namespace dirigent {
OnlineEvaluator::OnlineEvaluator(int nP, int id, std::shared_ptr<io::NetIOMP> network,
                                PreprocCircuit<dirigent::Field> preproc,
                                utils::LevelOrderedCircuit circ,
                                int security_param, int threads, int seed)
    : nP_(nP),
      id_(id),
      security_param_(security_param),
      rgen_(id,seed),
      neetwork_(std::move(network)),
      preproc_(preproc),
      circ_(std::move(circ)),
      wires_(circ.num_gates) {tpool_ = std::make_shared<ThreadPool>(threads); }

OnlineEvaluator::OnlineEvaluator(int nP, int id, std::shared_ptr<io::NetIOMP> network,
                                PreprocCircuit<dirigent::Field> preproc,
                                utils::LevelOrderedCircuit circ,
                                int security_param,
                                std::shared_ptr<ThreadPool> tpool, int seed)
    : nP_(nP),
      id_(id),
      security_param_(security_param),
      rgen_(id, seed),
      network_(std::move(network)),
      preproc_(std::move(preproc)),
      circ_(std::move(circ)),
      tpool_(std::move(tpool)),
      wires_(circ.num_gates) {}

void OnlineEvaluator::setInputs(const std::unordered_map<utils::wire_t, dirigent::Field>& inputs) {
    std::vector<dirigent::Field> q_values;
    std::vector<dirigent::Field> masked_values;
    std::vector<size_t> num_inp_pid(4, 0);

    // Input gates have depth 0
    for(auto& g : circ_.gates_by_level[0]) {
        if(g->type == utils::GateType::kInp) {
            auto* pre_input = static_cast<PreprocInput<dirigent::Field>*>(preproc_.gates[g->out].get());
            auto pid = pre_input->pid;

            num_inp_pid[pid]++;
            // All parties excluding TP sample a common random value r_in
            dirigent::Field r_in;
            rgen_.all_minus_0().random_data(&r_in, sizeof(dirigent::Field));
            if(pid == id_) {
                // pre_input->pid computes pre_input->mask + inputs.at(g->out) + r_in
                q_values.push_back(pre_input->mask_value + inputs.at(g->out) + r_in);
                network_->send(0, q_values.data(), q_values.size() * sizeof(dirigent::Field));
            }
            network_->recv(0, q_values.data(), q_values.size() * sizeof(dirigent::Field));
            wires_[g->out] = q_values[num_inp_pid[pid]] - r_in;
        }
    }

}

void OnlineEvaluator::setRandomInputs() {
    // Input gates have depth 0.
    for (auto& g : circ_.gates_by_level[0]) {
    if (g->type == utils::GateType::kInp) {
      rgen_.all().random_data(&wires_[g->out], sizeof(Ring));
    }
  }
}

void OnlineEvaluator::evaluateGatesAtDepth(size_t depth) {
    for (auto& gate : circ_.gates_by_level[depth]) {
        switch (gate->type) {
            case utils::GateType::kAdd: {
            auto* g = static_cast<utils::FIn2Gate*>(gate.get());
            wires_[g->out] = wires_[g->in1] + wires_[g->in2];
            break;
            }

            case utils::GateType::kSub: {
            auto* g = static_cast<utils::FIn2Gate*>(gate.get());
            wires_[g->out] = wires_[g->in1] - wires_[g->in2];
            break;
            }

            case utils::GateType::kMul: {
                // All parties excluding TP sample a common random value r_in
                dirigent::Field r_mul;
                rgen_.all_minus_0().random_data(&r_mul, sizeof(dirigent::Field));
                auto* g = static_cast<utils::FIn2Gate*>(gate.get());
                auto& m_in1 = preproc_.gates[g->in1]->mask;
                auto& m_in2 = preproc_.gates[g->in2]->mask;
                auto& tpm_in1 = preproc_.gates[g->in1]->tpmask;
                auto& tpm_in2 = preproc_.gates[g->in2]->tpmask;
                auto* pre_out = 
                    static_cast<PreprocMultGate<dirigent::Field>*>(preproc_gates[g->out].get());
                
                if(id_ != 0) {
                    auto q_share = pre_out->mask + pre_out->mask_prod - 
                                     m_in1 * wires_[g->in2] - m_in2 * wires_[g->in1];
                    q_share.add((wires_[g->in1] * wires_[g->in2] + r_mul), id_);
                    network_->send(0, q_share.data(), sizeof(dirigent::Field));
                }
                else
                    if(id_ == 0) {
                        dirigent::Field q_value = 0;
                        for(int i = 1; i <= nP_; ++i) {
                            network_.recv(1, q_share.value_.data(), sizeof(dirigent::Field));
                            q_value += q_share.value_;
                            network_.send(i, q_value.data(), sizeof(dirigent::Field));
                            }
                    }
                
                if(id_ != 0)   {
                    network_.recv(0, q_value.data(), sizeof(dirigent::Field));
                    wires_[g->out] = q_value - r_mul;
                }
                break;
            }
            case::utils::GateType::kDotprod: {
                // All parties excluding TP sample a common random value r_in
                dirigent::Field r_dotp;
                rgen_.all_minus_0().random_data(&r_mul, sizeof(dirigent::Field));

                auto* g = static_cast<utils::SIMDGate*>(gate.get());
                auto* pre_out =
                    static_cast<PreprocDotpGate<Ring>*>(preproc_.gates[g->out].get());
                if(id_ != 0) {
                    auto q_share = pre_out->mask + pre_out->mask_prod;
                    for(size_t i = 0; i < g->in1.size(); ++i) {
                        auto win1 = g->in1[i]; //masked value for left input wires
                        auto win2 = g->in2[i]; //masked value for right input wires
                        auto& m_in1 = preproc_.gates[win1]->mask; //masks for left wires
                        auto& m_in2 = preproc_.gates[win2]->mask; //masks for right wires
                        q_share -= m_in1 * wires_[win2] + m_in2* wires_[win1];
                        q_share.add((wires_[win1] * wires_[win2]), id_);                  
                    }
                    q_share.add(r_dotp, id_);
                }
                else if (id_ == 0) {
                    dirigent::Field q_value = 0;
                    for(int i = 1; i <= nP_; ++i) {
                        network_.recv(1, q_share.value_.data(), sizeof(dirigent::Field));
                        q_value += q_share.value_;
                        network_.send(i, q_value.data(), sizeof(dirigent::Field));
                    }
                }
                if(id_ != 0)   {
                    wires_[g->out] = q_value - r_mul;
                }
                break;
            }
        }
    }
}

std::vector<dirigent::Field> OnlineEvaluator::getOutputs() {
    // if id_ == 0 : send preproc_.gates[wout]->mask
    // if id_ != 1 : receive the above value and compute masked_value + mask
    std::vector<dirigent::Field> outvals(circ_.outputs.size());
    if (circ_.outputs.empty()) {
        return outvals;
    }

    std::vector<dirigent::Field> output_masks(circ_.output.size());
    if(id_ == 0) {
        for (size_t i = 0; i < circ_.outputs.size(); ++i) {
            auto wout = circ_.outputs[i];
            dirigent::Field outmask = preproc_.gates[wout]->tpmask.secret();
            output_masks.push_back(outmask);
        }    
        for(int i = 1; i <= nP_; ++i) {
            network_.send(i, output_masks.data(), output_masks.size() * sizeof(dirigent::Field));
        }
    }
    else {
        network_.recv(0,output_masks.data(), output_masks.size() * sizeof(dirigent::Field));
        for(size_t i = 0; i < circ_.output.size(); ++i) {
            auto wout = circ_.outputs[i]; 
            outvals[i] = wires_[wout] - output_masks[i];
        }
    }

    return outvals;
    
}

dirigent::Field OnlineEvaluator::reconstruct(AuthAddShare<dirigent::Field>& shares) {
    dirigent::Field reconstructed_value = 0;
    if(id_ != 0) {
        network_.send(0, shares.value_.data(), sizeof(dirigent::Field));
    }
    else if (id_ == 0) {
        for(size_t i = 1, i <= nP_; ++i) {
            network_.recv(i, shares.value_.data(), sizeof(dirigent::Field));
            reconstructed_value += shares.value_.data();
        }
    }
    return reconstructed_value;
}

std::vector<dirigent::Field> OnlineEvaluator::evaluateCircuit( const std::unordered_map<utils::wires_t, dirigent::Field>& inputs) {
    setInputs(inputs);
  for (size_t i = 0; i < circ_.gates_by_level.size(); ++i) {
    evaluateGatesAtDepth(i);
  }
  return getOutputs();
}

// Add verification step

}; //namespace dirigent