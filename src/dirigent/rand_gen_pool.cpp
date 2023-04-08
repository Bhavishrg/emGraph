#include "rand_gen_pool.h"

#include <algorithm>

#include "helpers.h"

namespace dirigent {

RandGenPool::RandGenPool(int my_id) : id_{my_id} {
  //all keys will be the same.  for different keys look at emp toolkit
}

emp::PRG& RandGenPool::self() { return k_self; }

emp::PRG& RandGenPool::all() { return k_all; }

emp::PRG& RandGenPool::all_minus_0() { return k_all_minus_0; }

emp::PRG& RandGenPool::p0() { return k_p0; }

}  // namespace dirigents
