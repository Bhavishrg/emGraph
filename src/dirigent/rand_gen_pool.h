#pragma once
#include <emp-tool/emp-tool.h>

#include <vector>
#include <algorithm>

#include "helpers.h"

namespace dirigent {

// Collection of PRGs.
class RandGenPool {
  //int id_;

  emp::PRG k_p0;
  emp::PRG k_self;
  emp::PRG k_all_minus_0;
  emp::PRG k_all;

 public:
  //explicit RandGenPool(int my_id, uint64_t seed = 200);
  explicit RandGenPool(uint64_t seed = 200);

  emp::PRG& self();
  emp::PRG& all_minus_0();
  emp::PRG& all();
  emp::PRG& p0();

};

};  // namespace dirigent
