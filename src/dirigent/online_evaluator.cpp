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



void OnlineEvaluator::evaluateGatesAtDepthPartySend(size_t depth, 
                                std::vector<Field>& mult_nonTP, std::vector<Field>& r_mult_pad,
                                std::vector<Field>& mult3_nonTP, std::vector<Field>& r_mult3_pad,
                                std::vector<Field>& mult4_nonTP, std::vector<Field>& r_mult4_pad,
                                std::vector<Field>& dotprod_nonTP, std::vector<Field>& r_dotprod_pad) {
    
    for (auto& gate : circ_.gates_by_level[depth]) {
        switch (gate->type) {
            case quadsquad::utils::GateType::kMul: {
                // All parties excluding TP sample a common random value r_in
                auto* g = static_cast<quadsquad::utils::FIn2Gate*>(gate.get());
                Field r_sum = 0;
                q_val_[g->out] = 0;
                
                if(id_ != 0) {
                    std::vector<Field> r_mul(nP_);
                    for( int i = 0; i < nP_; i++)   {
                        rgen_.all_minus_0().random_data(&r_mul[i], sizeof(Field));
                        r_sum += r_mul[i];
                    }
                    
                    r_mult_pad.push_back(r_sum);
                    
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
                    mult_nonTP.push_back(q_share.valueAt());
                    
                    q_sh_[g->out] = q_share;
                }
                
                break;
            }

            case quadsquad::utils::GateType::kMul3: {
                // All parties excluding TP sample a common random value r_in
                auto* g = static_cast<quadsquad::utils::FIn3Gate*>(gate.get());
                Field r_sum = 0;
                q_val_[g->out] = 0;

                if(id_ != 0) {
                    std::vector<Field> r_mul3(nP_);
                    for( int i = 0; i < nP_; i++)   {
                        rgen_.all_minus_0().random_data(&r_mul3[i], sizeof(Field));
                        r_sum += r_mul3[i];
                    }
                    r_mult3_pad.push_back(r_sum);
                    
                    auto& del_a = preproc_.gates[g->in1]->mask;
                    auto& del_b = preproc_.gates[g->in2]->mask;
                    auto& del_c = preproc_.gates[g->in3]->mask;

                    auto& m_a = wires_[g->in1];
                    auto& m_b = wires_[g->in2];
                    auto& m_c = wires_[g->in3];
                    
                    auto* pre_out = 
                    static_cast<PreprocMult3Gate<Field>*>(preproc_.gates[g->out].get());
                    
                    auto q_share = pre_out->mask;
                    
                    q_share -= pre_out->mask_abc;

                    q_share += (pre_out->mask_bc * m_a
                             + pre_out->mask_ac * m_b
                             + pre_out->mask_ab * m_c);
                    
                    q_share -= ( del_c * m_a * m_b
                               + del_b * m_a * m_c
                               + del_a * m_b * m_c); 
                    
                      
                    q_share.add(m_a * m_b * m_c, id_);
                    for (int i = 1; i <= nP_; i++)  {
                        q_share.addWithAdder(r_mul3[id_-1], id_, i);
                    }
                    mult3_nonTP.push_back(q_share.valueAt());
                    
                    q_sh_[g->out] = q_share;
                }
                break;
            }

            case quadsquad::utils::GateType::kMul4: {
                // All parties excluding TP sample a common random value r_in
                auto* g = static_cast<quadsquad::utils::FIn4Gate*>(gate.get());
                Field r_sum = 0;
                q_val_[g->out] = 0;
                
                if(id_ != 0) {
                    std::vector<Field> r_mul4(nP_);
                    for( int i = 0; i < nP_; i++)   {
                        rgen_.all_minus_0().random_data(&r_mul4[i], sizeof(Field));
                        r_sum += r_mul4[i];
                    }

                    r_mult4_pad.push_back(r_sum);

                    auto& del_a = preproc_.gates[g->in1]->mask;
                    auto& del_b = preproc_.gates[g->in2]->mask;
                    auto& del_c = preproc_.gates[g->in3]->mask;
                    auto& del_d = preproc_.gates[g->in4]->mask;

                    auto& m_a = wires_[g->in1];
                    auto& m_b = wires_[g->in2];
                    auto& m_c = wires_[g->in3];
                    auto& m_d = wires_[g->in4];

                    auto* pre_out = 
                    static_cast<PreprocMult4Gate<Field>*>(preproc_.gates[g->out].get());

                    auto q_share = pre_out->mask;

                    q_share -= (  del_d * m_a * m_b * m_c
                                + del_c * m_a * m_b * m_d
                                + del_b * m_a * m_c * m_d
                                + del_a * m_b * m_c * m_d);
                    
                    q_share += (pre_out->mask_cd * m_a * m_b 
                              + pre_out->mask_bd * m_a * m_c 
                              + pre_out->mask_bc * m_a * m_d 
                              + pre_out->mask_ad * m_b * m_c
                              + pre_out->mask_ac * m_b * m_d 
                              + pre_out->mask_ab * m_c * m_d);

                    q_share -= (pre_out->mask_bcd * m_a 
                             +  pre_out->mask_acd * m_b
                             +  pre_out->mask_abd * m_c
                             +  pre_out->mask_abc * m_d);

                    q_share += pre_out->mask_abcd;

                    q_share.add(m_a * m_b * m_c * m_d, id_);
                    for (int i = 1; i <= nP_; i++)  {
                        q_share.addWithAdder(r_mul4[id_-1], id_, i);
                    }
                    mult4_nonTP.push_back(q_share.valueAt());
                    
                    q_sh_[g->out] = q_share;
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

                    r_dotprod_pad.push_back(r_sum);
                
                    auto q_share = pre_out->mask + pre_out->mask_prod;
                    for(size_t i = 0; i < g->in1.size(); ++i) {
                        auto win1 = g->in1[i]; //index for masked value for left input wires
                        auto win2 = g->in2[i]; //index for masked value for right input wires
                        auto& m_in1 = preproc_.gates[win1]->mask; //masks for left wires
                        auto& m_in2 = preproc_.gates[win2]->mask; //masks for right wires
                        q_share -= (m_in1 * wires_[win2] + m_in2* wires_[win1]);
                        q_share.add((wires_[win1] * wires_[win2]), id_);                  
                    }
                    for (int i = 1; i <= nP_; i++)  {
                        q_share.addWithAdder(r_dotp[id_-1], id_, i);
                    }
                    dotprod_nonTP.push_back(q_share.valueAt());
                    q_sh_[g->out] = q_share;
                }
                break;
            }
        default:
            break;
        }
    }
}

void OnlineEvaluator::evaluateGatesAtDepthPartyRecv(size_t depth, 
                                    std::vector<Field> mult_all, std::vector<Field> r_mult_pad,
                                    std::vector<Field> mult3_all, std::vector<Field> r_mult3_pad,
                                    std::vector<Field> mult4_all, std::vector<Field> r_mult4_pad,
                                    std::vector<Field> dotprod_all, std::vector<Field> r_dotprod_pad){
    size_t idx_mult = 0;
    size_t idx_mult3 = 0;
    size_t idx_mult4 = 0;
    size_t idx_dotprod = 0;
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

            case quadsquad::utils::GateType::kConstAdd: {
                auto* g = static_cast<quadsquad::utils::ConstOpGate<Field>*>(gate.get());
                wires_[g->out] = wires_[g->in] + g->cval;
                break;
            }

            case quadsquad::utils::GateType::kConstMul: {
                auto* g = static_cast<quadsquad::utils::ConstOpGate<Field>*>(gate.get());
                wires_[g->out] = wires_[g->in] * g->cval;
                break;
            }

            case quadsquad::utils::GateType::kMul: {
                auto* g = static_cast<quadsquad::utils::FIn2Gate*>(gate.get());
                q_val_[g->out] = mult_all[idx_mult];
                wires_[g->out] = q_val_[g->out] - r_mult_pad[idx_mult];
                idx_mult++;
                break;
            }
            case quadsquad::utils::GateType::kMul3: {
                auto* g = static_cast<quadsquad::utils::FIn3Gate*>(gate.get());
                q_val_[g->out] = mult3_all[idx_mult3];
                wires_[g->out] = q_val_[g->out] - r_mult3_pad[idx_mult3];
                idx_mult3++;
                break;
            }
            case quadsquad::utils::GateType::kMul4: {
                auto* g = static_cast<quadsquad::utils::FIn4Gate*>(gate.get());
                q_val_[g->out] = mult4_all[idx_mult4];
                wires_[g->out] = q_val_[g->out] - r_mult4_pad[idx_mult4];
                idx_mult4++;
                break;
            }
            case quadsquad::utils::GateType::kDotprod: {
                auto* g = static_cast<quadsquad::utils::SIMDGate*>(gate.get());
                q_val_[g->out] = dotprod_all[idx_dotprod];
                wires_[g->out] = q_val_[g->out] - r_dotprod_pad[idx_dotprod];
                idx_dotprod++;
                break;
            }
            default:
            break;
        }
    }
}



void OnlineEvaluator::evaluateGatesAtDepth(size_t depth) { 
    size_t mult_num = 0; 
    size_t mult3_num = 0; 
    size_t mult4_num = 0; 
    size_t dotprod_num = 0; 

    for (auto& gate : circ_.gates_by_level[depth]) {
        switch (gate->type) {
            case quadsquad::utils::GateType::kMul: {
                mult_num++;
                break;
            }

            case quadsquad::utils::GateType::kMul3: {
                mult3_num++;
                break;
            }

            case quadsquad::utils::GateType::kMul4: {
                mult4_num++;
                break;
            }

            case quadsquad::utils::GateType::kDotprod: {
                dotprod_num++;
                break;
            }


        }
    }

    size_t total_comm = mult_num + mult3_num + mult4_num + dotprod_num;

    std::vector<Field> mult_nonTP;
    std::vector<Field> mult3_nonTP;
    std::vector<Field> mult4_nonTP;
    std::vector<Field> dotprod_nonTP;        

    std::vector<Field> r_mult_pad;
    std::vector<Field> r_mult3_pad;
    std::vector<Field> r_mult4_pad;
    std::vector<Field> r_dotprod_pad;

   

    if(id_ != 0) {
        evaluateGatesAtDepthPartySend(depth, 
                                        mult_nonTP, r_mult_pad,
                                        mult3_nonTP, r_mult3_pad,
                                        mult4_nonTP, r_mult4_pad,
                                        dotprod_nonTP, r_dotprod_pad);
        

        
        std::vector<Field> online_comm_to_TP(total_comm);
        
        for(size_t i = 0; i < mult_num; i++) {
            online_comm_to_TP[i] = mult_nonTP[i];
        }
        for(size_t i = 0; i < mult3_num; i++) {
            online_comm_to_TP[i + mult_num] = mult3_nonTP[i];
        }
        for(size_t i = 0; i < mult4_num; i++) {
            online_comm_to_TP[i + mult_num + mult3_num] = mult4_nonTP[i];
        }
        for(size_t i = 0; i < dotprod_num; i++ ) {
            online_comm_to_TP[i + mult_num + mult3_num + mult4_num]
                                        = dotprod_nonTP[i];
        }

        network_->send(0, online_comm_to_TP.data(), sizeof(Field) * total_comm);

    }
    else if (id_ == 0) { 
        std::vector<Field> online_comm_to_TP(total_comm);
        std::vector<Field> agg_values(total_comm);
        
        for(int i = 0; i < total_comm; i++) {
            agg_values[i] = 0;
            for(int pid = 1; pid <= nP_; pid++) {
                network_->recv(pid, online_comm_to_TP.data(), sizeof(Field) * total_comm);
                agg_values[i] += online_comm_to_TP[i];
                
            }
        }

        
        for(int pid = 1; pid <= nP_; pid++){
            network_->send(pid, agg_values.data(), sizeof(Field) * total_comm);
        }
    }

    if(id_ != 0 ) {
        std::vector<Field> agg_values(total_comm);
        network_->recv(0, agg_values.data(), sizeof(Field) * total_comm);

        std::vector<Field> mult_all(mult_num);
        std::vector<Field> mult3_all(mult3_num);
        std::vector<Field> mult4_all(mult4_num);
        std::vector<Field> dotprod_all(dotprod_num);

        for(size_t i = 0; i < mult_num; i++) {
            mult_all[i] = agg_values[i];
        }
        for(size_t i = 0; i < mult3_num; i++) {
            mult3_all[i] = agg_values[mult_num + i];
        }
        for(size_t i = 0; i < mult4_num; i++) {
            mult4_all[i] = agg_values[mult3_num + mult_num + i];
        }
        for(size_t i = 0; i < dotprod_num; i++) {
            dotprod_all[i] = agg_values[mult4_num + 
                                    mult3_num + 
                                    mult_num + i];
        }
        evaluateGatesAtDepthPartyRecv(depth, 
                                        mult_all, r_mult_pad,
                                        mult3_all, r_mult3_pad,
                                        mult4_all, r_mult4_pad,
                                        dotprod_all, r_dotprod_pad);
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
                    }
                    case quadsquad::utils::GateType::kDotprod: {
                        auto* g = static_cast<quadsquad::utils::SIMDGate*>(gate.get());
                        prg.random_data(&rho[g->out], sizeof(Field));
                        omega += rho[g->out] * (q_val_[g->out] * key - q_sh_[g->out].tagAt());
                    }
                    case quadsquad::utils::GateType::kMul3: {
                        auto* g = static_cast<quadsquad::utils::FIn3Gate*>(gate.get());
                        prg.random_data(&rho[g->out], sizeof(Field));
                        omega += rho[g->out] * (q_val_[g->out] * key - q_sh_[g->out].tagAt());
                    }
                    case quadsquad::utils::GateType::kMul4: {
                        auto* g = static_cast<quadsquad::utils::FIn4Gate*>(gate.get());
                        prg.random_data(&rho[g->out], sizeof(Field));
                        omega += rho[g->out] * (q_val_[g->out] * key - q_sh_[g->out].tagAt());
                    }
                    case quadsquad::utils::GateType::kConstAdd:
                    case quadsquad::utils::GateType::kConstMul:
                    case quadsquad::utils::GateType::kAdd:
                    case quadsquad::utils::GateType::kSub: {
                        break;
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