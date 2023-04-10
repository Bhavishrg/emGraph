#pragma once
#include <emp-tool/emp-tool.h>

#include <vector>
#include <algorithm>

#include "helpers.h"

namespace dirigent {

// Collection of PRGs.
class RandGenPool {
  int id_;

  emp::PRG k_p0;
  emp::PRG k_self;
  emp::PRG k_all_minus_0;
  emp::PRG k_all;

 public:
  //explicit RandGenPool(int my_id, uint64_t seed = 200);
  RandGenPool(int my_id, uint64_t seed = 200) : id_{my_id} { 
    auto seed_block = emp::makeBlock(seed, 0); 
    k_self.reseed(&seed_block, 0);
    k_all.reseed(&seed_block, 1);
    k_all_minus_0.reseed(&seed_block, 2);
    k_p0.reseed(&seed_block, 3);
  }

  emp::PRG& self() { return k_self; }
  emp::PRG& all_minus_0(){ return k_all_minus_0; }
  emp::PRG& all(){ return k_all; }
  emp::PRG& p0() { return k_p0; }

};

};  // namespace dirigent
