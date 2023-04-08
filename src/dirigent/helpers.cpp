#include "helpers.h"

#include <NTL/ZZ_pX.h>

#include <cmath>

namespace dirigent {

std::vector<uint64_t> packBool(const bool* data, size_t len) {
  std::vector<uint64_t> res;
  for (size_t i = 0; i < len;) {
    uint64_t temp = 0;
    for (size_t j = 0; j < 64 && i < len; ++j, ++i) {
      if (data[i]) {
        temp |= (0x1ULL << j);
      }
    }
    res.push_back(temp);
  }

  return res;
}

void unpackBool(const std::vector<uint64_t>& packed, bool* data, size_t len) {
  for (size_t i = 0, count = 0; i < len; count++) {
    uint64_t temp = packed[count];
    for (int j = 63; j >= 0 && i < len; ++i, --j) {
      data[i] = (temp & 0x1) == 0x1;
      temp >>= 1;
    }
  }
}



};  // namespace dirigent
