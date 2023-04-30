#pragma once

#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pE.h>
#include <emp-tool/emp-tool.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../io/netmp.h"
#include "../dirigent/types.h"

namespace dirigent{

// Supports only native int type.
template <class R>
std::vector<BoolRing> bitDecompose(R val) {
  auto num_bits = sizeof(val) * 8;
  std::vector<BoolRing> res(num_bits);
  for (size_t i = 0; i < num_bits; ++i) {
    res[i] = ((val >> i) & 1ULL) == 1;
  }

  return res;
}

// size_t perfectPow(size_t val) {
//   size_t temp = val;
//   std::vector<int> bits;
//   int len = 0;
//   while (temp > 0)
//   {
//     bits.push_back(temp%2);
//     temp = temp/2;
//     len++;
//   }
//   size_t res = pow(2, len);
//   return res;
// }

std::vector<uint64_t> packBool(const bool* data, size_t len);
void unpackBool(const std::vector<uint64_t>& packed, bool* data, size_t len);
};  // namespace dirigent