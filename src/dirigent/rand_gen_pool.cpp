#include "rand_gen_pool.h"

#include <algorithm>

#include "helpers.h"

namespace dirigent {

  //RandGenPool::RandGenPool(int my_id, uint64_t seed) : id_{my_id} { 
  //auto seed_block = emp::makeBlock(seed, 0); 
  //k_self.reseed(&seed_block, 0);
  //k_all.reseed(&seed_block, 0);
  //k_all_minus_0.reseed(&seed_block, 0);
  //k_p0.reseed(seed, 0);
  //}
  //all keys will be the same.  for different keys look at emp toolkit

emp::PRG& RandGenPool::self() { return k_self; }

emp::PRG& RandGenPool::all() { return k_all; }

emp::PRG& RandGenPool::all_minus_0() { return k_all_minus_0; }

emp::PRG& RandGenPool::p0() { return k_p0; }

}  // namespace dirigents
