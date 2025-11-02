#include "sharing.h"

namespace grasp {
//check the correctness of the following functions: 
template <>
void AddShare<BoolRing>::randomize(emp::PRG& prg) {
 bool data[1];
 prg.random_bool(static_cast<bool*>(data), 1);
 value_ = data[0];
}


};  // namespace grasp
