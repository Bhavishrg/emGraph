#include"online_evaluator.h"

#include <array>

#include"dirigent/helpers.h"

namespace dirigent {
OnlineEvaluator::OnlineEvaluator(int nP, int id, std::shared_ptr<io::NetIOMP> network,
                                PreprocCircuit<Field> preproc,
                                quadsquad::utils::LevelOrderedCircuit circ,
                                int security_param, int threads, int seed)
    : nP_(nP),
      id_(id),
      security_param_(security_param),
      rgen_(id,seed),
      network_(std::move(network)),
      preproc_(std::move(preproc)),
      circ_(std::move(circ)),
      wires_(circ.num_gates),
      q_sh_(circ.num_gates),
      q_val_(circ.num_gates)
      {tpool_ = std::make_shared<ThreadPool>(threads); }

OnlineEvaluator::OnlineEvaluator(int nP, int id, std::shared_ptr<io::NetIOMP> network,
                                PreprocCircuit<Field> preproc,
                                quadsquad::utils::LevelOrderedCircuit circ,
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
      wires_(circ.num_gates),
      q_sh_(circ.num_gates),        
      q_val_(circ.num_gates) {}

void OnlineEvaluator::setInputs(const std::unordered_map<quadsquad::utils::wire_t, Field>& inputs) {
    //Field q_value=0;
    std::vector<Field> masked_values;
    std::vector<size_t> num_inp_pid(nP_, 0);
    
    // Input gates have depth 0
    for(auto& g : circ_.gates_by_level[0]) {
        if(g->type == quadsquad::utils::GateType::kInp) {
            auto* pre_input = static_cast<PreprocInput<Field>*>(preproc_.gates[g->out].get());
            auto pid = pre_input->pid;
            
            num_inp_pid[pid]++;
            Field r_in;
            // All parties excluding TP sample a common random value r_in
            if(id_ != 0) {
                
                rgen_.all_minus_0().random_data(&r_in, sizeof(Field));
                
                if(pid == id_) {
                // pre_input->pid computes pre_input->mask + inputs.at(g->out) + r_in
                    q_val_[g->out] = pre_input->mask_value + inputs.at(g->out) + r_in;
                    network_->send(0, &q_val_[g->out], sizeof(Field));
                    
                }
            }
            else if(id_ == 0) {
                network_->recv(pid, &q_val_[g->out], sizeof(Field));
                for(int i = 1; i <= nP_; i++) {
                    network_->send(i, &q_val_[g->out], sizeof(Field));
                }
            }
            if(id_ != 0) {
                network_->recv(0, &q_val_[g->out], sizeof(Field));
                wires_[g->out] = q_val_[g->out] - r_in;
            }
            
        }
    }

}

void OnlineEvaluator::setRandomInputs() {
    // Input gates have depth 0.
    for (auto& g : circ_.gates_by_level[0]) {
    if (g->type == quadsquad::utils::GateType::kInp) {
      rgen_.all().random_data(&wires_[g->out], sizeof(Field));
    }
  }
}

void OnlineEvaluator::evaluateGatesAtDepth(size_t depth) {
    
    for (auto& gate : circ_.gates_by_level[depth]) {
        switch (gate->type) {
            case quadsquad::utils::GateType::kAdd: {
            auto* g = static_cast<quadsquad::utils::FIn2Gate*>(gate.get());
            if(id_!= 0) wires_[g->out] = wires_[g->in1] + wires_[g->in2];
            q_val_[g->out] = 0;
            break;
            }

            case quadsquad::utils::GateType::kSub: {
            auto* g = static_cast<quadsquad::utils::FIn2Gate*>(gate.get());
            if(id_ != 0) wires_[g->out] = wires_[g->in1] - wires_[g->in2];
            q_val_[g->out] = 0;
            break;
            }

            case quadsquad::utils::GateType::kMul: {
                // All parties excluding TP sample a common random value r_in
                auto* g = static_cast<quadsquad::utils::FIn2Gate*>(gate.get());
                Field r_sum = 0;
                q_val_[g->out] = 0;
                std::vector<Field> qi(nP_ + 1);
                
                if(id_ != 0) {
                    std::vector<Field> r_mul(nP_);
                    for( int i = 0; i < nP_; i++)   {
                        rgen_.all_minus_0().random_data(&r_mul[i], sizeof(Field));
                        r_sum += r_mul[i];
                    }
                    
                    auto& m_in1 = preproc_.gates[g->in1]->mask;
                    auto& m_in2 = preproc_.gates[g->in2]->mask;
                    auto* pre_out = 
                    static_cast<PreprocMultGate<Field>*>(preproc_.gates[g->out].get());
                    auto q_share = pre_out->mask + pre_out->mask_prod - 
                                     m_in1 * wires_[g->in2] - m_in2 * wires_[g->in1];
                    q_share.add((wires_[g->in1] * wires_[g->in2]), id_);
                    for (int i = 1; i <= nP_; i++)  {
                        q_share.addWithAdder(r_mul[id_-1], id_, i);
                    }
                    network_->send(0, &q_share.valueAt(), sizeof(Field));
                    q_sh_[g->out] = q_share;
                    std::cout<< "q_sh[" << g->out <<"].value =  " << q_sh_[g->out].valueAt() <<std::endl;
                    std::cout<< "q_sh[" << g->out <<"].tag =  " << q_sh_[g->out].tagAt() <<std::endl;
                    std::cout<< "q_sh[" << g->out <<"].key =  " << q_sh_[g->out].keySh() <<std::endl;
                }
                else
                    if(id_ == 0) { 
                        q_val_[g->out] = 0;
                        for(int i = 1; i <= nP_; ++i) {
                            
                            Field q = 0;
                            network_->recv(i, &q, sizeof(q));
                            
                            q_val_[g->out] += q;
                        }
                        for(int i = 1; i <= nP_; i++) {
                            network_->send(i, &q_val_[g->out], sizeof(Field));
                        }
                        
                    }
                if(id_ != 0) {
                    network_->recv(0, &q_val_[g->out], sizeof(Field));
                    std::cout<< "q_val[" << g->out << "] =  "<< q_val_[g->out] <<std::endl; 
                    wires_[g->out] = q_val_[g->out] - r_sum;
                }
                break;
            }
            
            case::quadsquad::utils::GateType::kDotprod: {
                // All parties excluding TP sample a common random value r_in
                
                Field r_sum = 0;
                
                

                auto* g = static_cast<quadsquad::utils::SIMDGate*>(gate.get());
                auto* pre_out =
                    static_cast<PreprocDotpGate<Field>*>(preproc_.gates[g->out].get());
                if(id_ != 0) {
                    std::vector<Field> r_dotp(nP_);
                    for( int i = 0; i < nP_; i++)   {
                        rgen_.all_minus_0().random_data(&r_dotp[i], sizeof(Field));
                        r_sum += r_dotp[i];
                    }
                
                    auto q_share = pre_out->mask + pre_out->mask_prod;
                    for(size_t i = 0; i < g->in1.size(); ++i) {
                        auto win1 = g->in1[i]; //masked value for left input wires
                        auto win2 = g->in2[i]; //masked value for right input wires
                        auto& m_in1 = preproc_.gates[win1]->mask; //masks for left wires
                        auto& m_in2 = preproc_.gates[win2]->mask; //masks for right wires
                        q_share -= (m_in1 * wires_[win2] + m_in2* wires_[win1]);
                        q_share.add((wires_[win1] * wires_[win2]), id_);                  
                    }
                    q_share.addWithAdder(r_dotp[id_-1], id_, id_);
                    network_->send(0, &q_share.valueAt(), sizeof(Field));
                    q_sh_[g->out] = q_share;
                }
                else if (id_ == 0) {
                    
                    q_val_[g->out] = 0;
                    for(int i = 1; i <= nP_; ++i) {
                        Field q_share=0;
                        network_->recv(i, &q_share, sizeof(Field));
                        q_val_[g->out] += q_share;
                    }
                    for(int i = 1; i <= nP_; i++) {
                        network_->send(i, &q_val_[g->out], sizeof(Field));
                    }
                }
                if(id_ != 0) {
                    //Field q_value;
                    network_->recv(0, &q_val_[g->out], sizeof(Field));
                    wires_[g->out] = q_val_[g->out] - r_sum;
                }
                break;
            }
        default:
            break;
        }
    }
}



bool OnlineEvaluator::MACVerification() {
    emp::block cc_key[2];
    if (id_ == 0) {
        rgen_.self().random_block(cc_key, 2);
        for (int i = 1; i <= nP_; i++) {
            network_->send(i, cc_key, 2 * sizeof(emp::block));
        }
    } else {
        network_->recv(0, cc_key, 2 * sizeof(emp::block));
    }
    emp::PRG prg;
    prg.reseed(cc_key);
    Field res = 0;
    if(id_ != 0) {
        Field key = preproc_.gates[0]->mask.keySh();
        int m = circ_.num_gates;
        Field omega = 0;
        std::unordered_map<quadsquad::utils::wire_t, Field> rho;

        for (size_t i = 0; i < circ_.gates_by_level.size(); ++i) {
            for (auto& gate : circ_.gates_by_level[i]) {
                switch (gate->type) {
                    case quadsquad::utils::GateType::kMul: {
                        auto* g = static_cast<quadsquad::utils::FIn2Gate*>(gate.get());
                        prg.random_data(&rho[g->out], sizeof(Field));
                        omega += rho[g->out] * (q_val_[g->out] * key - q_sh_[g->out].tagAt());
                        std::cout<< omega << std::endl;
                    }
                }
            }
        }
        network_->send(0, &omega, sizeof(Field));
    }
    else {
        Field omega;
        
        for (int i = 1; i <= nP_; i++) {
            network_->recv(i, &omega, sizeof(Field));
            res += omega;
        }
        for (int i = 1; i <= nP_; i++) {
            network_->send(i, &res, sizeof(Field));
        }
    }
    if( id_ != 0) {
        
        network_->recv(0, &res, sizeof(Field));
    }

    if(res == 0){ return true; }
    else { return false; }
}

std::vector<Field> OnlineEvaluator::getOutputs() {
    // if id_ == 0 : send preproc_.gates[wout]->mask
    // if id_ != 1 : receive the above value and compute masked_value + mask
    std::vector<Field> outvals(circ_.outputs.size());
    if (circ_.outputs.empty()) {
        return outvals;
    }

    std::vector<Field> output_masks(circ_.outputs.size());
    if(id_ == 0) {
        for (size_t i = 0; i < circ_.outputs.size(); ++i) {
            auto wout = circ_.outputs[i];
            Field outmask = preproc_.gates[wout]->tpmask.secret();
            output_masks.push_back(outmask);
            for(int i = 1; i <= nP_; ++i) {
            network_->send(i, &outmask, sizeof(Field));
        }
    }    
        //for(int i = 1; i <= nP_; ++i) {
        //    network_->send(i, &output_masks, output_masks.size() * sizeof(Field));
        //}
        return output_masks;
    }
    else {
        std::vector<Field> output_masks(circ_.outputs.size());
        // network_->recv(0, &output_masks, output_masks.size() * sizeof(Field));
        for(size_t i = 0; i < circ_.outputs.size(); ++i) {
            Field outmask;
            network_->recv(0, &outmask, sizeof(Field));
            auto wout = circ_.outputs[i]; 
            //outvals[i] = wires_[wout] - output_masks[i];
            outvals[i] = wires_[wout] - outmask; 
        }
        return outvals;
    }

    
    
}

Field OnlineEvaluator::reconstruct(AuthAddShare<Field>& shares) {
    Field reconstructed_value = 0;
    if(id_ != 0) {
        network_->send(0, &shares.valueAt(), sizeof(Field));
    }
    else if (id_ == 0) {
        
        for(size_t i = 1; i <= nP_; ++i) {    
            std::vector<Field> share_val;
            network_->recv(i, &share_val[i], sizeof(Field));
            reconstructed_value += share_val[i];
        }
    }
    return reconstructed_value;
}

std::vector<Field> OnlineEvaluator::evaluateCircuit( const std::unordered_map<quadsquad::utils::wire_t, Field>& inputs) {
    setInputs(inputs);
    
  for (size_t i = 0; i < circ_.gates_by_level.size(); ++i) {
    evaluateGatesAtDepth(i); 
  }
  
    if(MACVerification()) { return getOutputs(); }
    else { 
        std::cout<< "Malicious Activity Detected!!!" << std::endl;
        std::vector<Field> abort (circ_.outputs.size(), 0);
        return abort;
        }

}

// Add verification step

}; //namespace dirigent