#pragma once

#include <boost/multi_array.hpp>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>

#include "types.h"
#include "circuit.h"

namespace common::utils {
struct Order {
    wire_t name;
    wire_t unit;
    wire_t price;

    Order() = default;
    Order(wire_t name, wire_t unit, wire_t price)
        :   name{name}, unit{unit}, price{price} {}
};
struct BuyList{
    std::vector<Order> order;

    BuyList() = default;
    BuyList(std::vector<Order> order)
        : order(std::move(order)) {}
};

struct SellList{
    std::vector<Order> order;

    SellList() = default;
    SellList(std::vector<Order> order)
        : order(std::move(order)) {}
};

template <typename R>
class DarkPool {
    Circuit<R> circ_;
    
    SellList s_list_;
    BuyList b_list_;
    size_t sell_list_size_;
    size_t buy_list_size_;
    Order new_order_;
    

    public:
     explicit DarkPool(size_t sell_list_size, size_t buy_list_size)
        :   sell_list_size_{sell_list_size}, 
            buy_list_size_{buy_list_size} {}
    
    void resizeList() {
        s_list_.order.resize(sell_list_size_);
        b_list_.order.resize(buy_list_size_);
    }
    
    const Circuit<R>& getCDACircuit() {
        size_t N = s_list_.order.size();
        size_t M = b_list_.order.size();
        
        R zero = 0;
        R one = 1;
        R neg_one = -1;

        new_order_.name = circ_.newInputWire();
        new_order_.unit = circ_.newInputWire();
        new_order_.price = circ_.newInputWire();

        s_list_.order[0].name = circ_.addConstOpGate(GateType::kConstAdd, new_order_.name, one);
        s_list_.order[0].unit = circ_.addConstOpGate(GateType::kConstAdd, new_order_.unit, one);
        s_list_.order[0].price = circ_.addConstOpGate(GateType::kConstAdd, new_order_.price, one);

        for(size_t i = 1; i < N; i++) {
            s_list_.order[i].name = circ_.addConstOpGate(GateType::kConstAdd, s_list_.order[i-1].name, one);
            s_list_.order[i].unit = circ_.addConstOpGate(GateType::kConstAdd, s_list_.order[i-1].unit, one);
            s_list_.order[i].price = circ_.addConstOpGate(GateType::kConstAdd, s_list_.order[i-1].price, one);
        }

        b_list_.order[0].name = circ_.addConstOpGate(GateType::kConstAdd, new_order_.name, one);
        b_list_.order[0].unit = circ_.addConstOpGate(GateType::kConstAdd, new_order_.unit, one);
        b_list_.order[0].price = circ_.addConstOpGate(GateType::kConstAdd, new_order_.price, one);

        for(size_t i = 0; i < M; i++) {
            b_list_.order[i].name = circ_.addConstOpGate(GateType::kConstAdd, b_list_.order[i-1].name, one);
            b_list_.order[i].unit = circ_.addConstOpGate(GateType::kConstAdd, b_list_.order[i-1].unit, one);
            b_list_.order[i].price = circ_.addConstOpGate(GateType::kConstAdd, b_list_.order[i-1].price, one);
        }

        // start PSL
        std::vector<wire_t> w(N + 1);
        w[0] = circ_.addConstOpGate(GateType::kConstMul, s_list_.order[0].unit, zero);
        for(size_t i = 1; i <= N; i++) {
            w[i] = circ_.addGate(GateType::kAdd, s_list_.order[i-1].unit, w[i-1]);
        }

        std::vector<wire_t> z(N + 1);
        std::vector<wire_t> z_dash(N + 1);
        z[0] = circ_.addConstOpGate(GateType::kConstMul, w[0], zero);
        z_dash[0] = circ_.addConstOpGate(GateType::kConstMul, w[0], zero);
        for(size_t i = 1; i <= N; i++) {
            auto temp1 = circ_.addGate(GateType::kSub, w[i-1], new_order_.unit);
            auto temp2 = circ_.addConstOpGate(GateType::kConstAdd, new_order_.price, one);
            auto temp3 = circ_.addGate(GateType::kSub, s_list_.order[i-1].price, temp2);
            z[i] = circ_.addGate(GateType::kLtz, temp1);
            z_dash[i] = circ_.addGate(GateType::kLtz, temp3);
        }
        std::vector<wire_t> f(N + 1);
        f[0] = circ_.addConstOpGate(GateType::kConstMul, z[0], zero);
        for(size_t i = 1; i <= N; i++) {
            f[i] = circ_.addGate(GateType::kMul, z[i], z_dash[i]);
            // circ_.setAsOutput(f[i]);
        }
        size_t k = N/2;
        // for(size_t i = 0; i < k; i++) {
        //     circ_.setAsOutput(s_list_.order[i].name);
        //     circ_.setAsOutput(s_list_.order[i].unit);
        //     circ_.setAsOutput(s_list_.order[i].price);
        // }
        wire_t s_dash;
        auto tmp1 = circ_.addGate(GateType::kSub, new_order_.unit, w[k-1]);
        auto tmp2 = circ_.addGate(GateType::kSub, tmp1, s_list_.order[k-1].unit);
        auto tmp3 = circ_.addGate(GateType::kMul, tmp2, z[k+1]);
        s_dash = circ_.addGate(GateType::kAdd, tmp3, s_list_.order[k-1].unit);
        // end PSL 

        // start EQZ
        wire_t e = circ_.addGate(GateType::kEqz, new_order_.unit);
        // end EQZ

        // start ObSel
        wire_t q_0 = circ_.addGate(GateType::kMul, new_order_.price, e);
        new_order_.price = q_0;
        // end ObSel

        // start Insert
        buy_list_size_ += 1;
        b_list_.order[M].name = circ_.addConstOpGate(GateType::kConstMul, b_list_.order[0].name, zero);
        b_list_.order[M].unit = circ_.addConstOpGate(GateType::kConstMul, b_list_.order[0].unit, zero);
        b_list_.order[M].price = circ_.addConstOpGate(GateType::kConstMul, b_list_.order[0].price, zero);

        // f computation
        std::vector<wire_t> g(M+2);
        auto tmp4 = circ_.addGate(GateType::kSub, new_order_.price, b_list_.order[0].price);
        g[0] = circ_.addGate(GateType::kLtz, tmp4);
        for(size_t i = 1; i <= M+1; i++) {
            auto temp1 = circ_.addConstOpGate(GateType::kConstAdd, b_list_.order[i].price, one);
            auto temp2 = circ_.addGate(GateType::kSub, new_order_.price, temp1);
            g[i] = circ_.addGate(GateType::kLtz, temp2);
        }

        // f' computation
        std::vector<wire_t> h(M+2);
        std::vector<wire_t> tmp6(M+2);
        auto tmp5 = circ_.addConstOpGate(GateType::kConstMul, g[0], neg_one);
        tmp6[0] = circ_.addConstOpGate(GateType::kConstAdd, tmp5, one);
        h[0] = circ_.addGate(GateType::kMul, tmp6[0], g[0]);
        for(size_t i = 1; i <= M+1; i++) {
            tmp5 = circ_.addConstOpGate(GateType::kConstMul, g[i], neg_one);
            tmp6[i] = circ_.addConstOpGate(GateType::kConstAdd, tmp5, one);
            h[i] = circ_.addGate(GateType::kMul, tmp6[i], g[i-1]);
        }

        // f'' computation
        std::vector<wire_t> u(M+2);
        std::vector<wire_t> tmp8(M+2);
        auto tmp7 = circ_.addConstOpGate(GateType::kConstMul, h[0], neg_one);
        tmp8[0] = circ_.addConstOpGate(GateType::kConstAdd, tmp7, one);
        u[0] = circ_.addGate(GateType::kMul, tmp8[0], tmp6[0]);
        for(size_t i = 1; i <= M+1; i++) {
            tmp7 = circ_.addConstOpGate(GateType::kConstMul, h[i], neg_one);
            tmp8[i] = circ_.addConstOpGate(GateType::kConstAdd, tmp7, one);
            u[i] = circ_.addGate(GateType::kMul, tmp8[i], tmp6[i]);
        }

        std::vector<Order> updated_buy_list(M + 2);
        updated_buy_list[0].name = circ_.addConstOpGate(GateType::kConstMul, b_list_.order[0].name, zero);
        updated_buy_list[0].unit = circ_.addConstOpGate(GateType::kConstMul, b_list_.order[0].unit, zero);
        updated_buy_list[0].price = circ_.addConstOpGate(GateType::kConstMul, b_list_.order[0].price, zero);

        for(size_t i = 1; i <= M + 1; i++) {
            auto new_tmp1 = circ_.addGate(GateType::kMul, g[i - 1],  b_list_.order[i].name);
            auto new_tmp2 = circ_.addGate(GateType::kMul, h[i - 1],  b_list_.order[0].name);
            auto new_tmp3 = circ_.addGate(GateType::kMul, u[i - 1],  new_order_.name);
            auto new_tmp4 = circ_.addGate(GateType::kAdd, new_tmp1, new_tmp2);

            updated_buy_list[i].name = circ_.addGate(GateType::kAdd, new_tmp3, new_tmp4);

            new_tmp1 = circ_.addGate(GateType::kMul, g[i - 1],  b_list_.order[i].unit);
            new_tmp2 = circ_.addGate(GateType::kMul, h[i - 1],  b_list_.order[0].unit);
            new_tmp3 = circ_.addGate(GateType::kMul, u[i - 1],  new_order_.unit);
            new_tmp4 = circ_.addGate(GateType::kAdd, new_tmp1, new_tmp2);

            updated_buy_list[i].unit = circ_.addGate(GateType::kAdd, new_tmp3, new_tmp4);

            new_tmp1 = circ_.addGate(GateType::kMul, g[i - 1],  b_list_.order[i].price);
            new_tmp2 = circ_.addGate(GateType::kMul, h[i - 1],  b_list_.order[0].price);
            new_tmp3 = circ_.addGate(GateType::kMul, u[i - 1],  new_order_.price);
            new_tmp4 = circ_.addGate(GateType::kAdd, new_tmp1, new_tmp2);

            updated_buy_list[i].price = circ_.addGate(GateType::kAdd, new_tmp3, new_tmp4);
        }
        // end Insert


        return circ_;
    }
    


};
}  // namespace common::utils