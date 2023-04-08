#pragma once
#include <emp-tool/emp-tool.h>

#include <vector>

namespace dirigent {

// Collection of PRGs.
class RandGenPool {
  int id_;

  // v_rgen_[i] denotes PRG common with party i.
  //   v_rgen_[id_] is PRG not common with any party.
  // v_rgen_[i + 4] denotes PRG common with all parties except i.
  //   v_rgen_[id_ + 4] is PRG common with all parties.
  // std::vector<emp::PRG> v_rgen_;
  emp::PRG k_p0;
  emp::PRG k_self;
  emp::PRG k_all_minus_0;
  emp::PRG k_all;

 public:
  explicit RandGenPool(int my_id, uint64_t seed = 200);

  emp::PRG& self();
  emp::PRG& all_minus_0();
  emp::PRG& all();
  emp::PRG& p0();

};

};  // namespace dirigent
