#pragma once

#include <emp-tool/emp-tool.h>

#include <array>
#include <vector>

#include "helpers.h"
#include "types.h"

namespace dirigent {

template <class R>
class AuthAddShare {
  // key_sh is the additive share of the key used for the MAC
  // value_ will be additive share of my_id and tag_ will be the additive share of of the tag for my_id.
  R key_sh_;
  R value_;
  R tag_;
  
 public:
  AuthAddShare() = default;
  explicit AuthAddShare(R key_sh, R value, R tag)
      : key_sh_{key_sh}, value_{value}, tag_{tag} {}

  R& valueAt() { return value_; }
  R& tagAt() { return tag_; }
  
  R valueAt() const { return value_; }
  R tagAt() const { return tag_; }
  
  //Check this part
  //void randomize(emp::PRG& prg) {
  //  prg.random_data(values_.data(), sizeof(R) * 3); // This step is not clear 
  //}

  
//What is the function?
  //[[nodiscard]] R sum() const { return values_[0] + values_[1] + values_[2]; }

  // Arithmetic operators.
  AuthAddShare<R>& operator+=(const AuthAddShare<R>& rhs) {
    value_ += rhs.value_;
    tag_ += rhs.tag_;
    return *this;
  }

  // what is "friend"?
  friend AuthAddShare<R> operator+(AuthAddShare<R> lhs,
                                      const AuthAddShare<R>& rhs) {
    lhs += rhs;
    return lhs;
  }

  AuthAddShare<R>& operator-=(const AuthAddShare<R>& rhs) {
    (*this) += (rhs * -1);
    return *this;
  }

  friend AuthAddShare<R> operator-(AuthAddShare<R> lhs,
                                      const AuthAddShare<R>& rhs) {
    lhs -= rhs;
    return lhs;
  }

  AuthAddShare<R>& operator*=(const R& rhs) {
    value_ *= rhs;
    tag_ *= rhs;
    return *this;
  }

  friend AuthAddShare<R> operator*(AuthAddShare<R> lhs, const R& rhs) {
    lhs *= rhs;
    return lhs;
  }

  AuthAddShare<R>& add(R val, int pid) {
    if (pid == 1) {
      value_ += val;
      tag_ += key_sh_.val;
    } else {
      tag_ += key_sh_.val;
    }

    return *this;
  }
  
};

template <class R>
class TPShare {
  R key_;
  std::vector<R> key_sh_;
  std::vector<R> values_;
  std::vector<R> tags_;

  public:
  TPShare() = default;
  explicit TPShare(R key, std::vector<R> key_sh, std::vector<R> value, std::vector<R> tag)
      : key_{key}, key_sh_{key_sh}, values_{std::move(value)}, tags_{std::move(tag)} {}

  // Access share elements.
  // idx = i retreives value common with party having i.
  R& operator[](size_t idx) { return values_.at(idx); }
  // idx = i retreives tag common with party having i.
  //R& operator()(size_t idx) { return tags_.at(idx); }
  
  R operator[](size_t idx) const { return values_.at(idx); }
  //R operator()(size_t idx) { return tags_.at(idx); }

  R& commonValueWithParty(int pid) {
    return values_.at(pid);
  }

  R& commonTagWithParty(int pid) {
    return tags_.at(pid);
  }

  [[nodiscard]] R commonValueWithParty(int pid) const {
    return values_.at(pid);
  }

  [[nodiscard]] R commonTagWithParty(int pid) const {
    return tags_.at(pid);
  }

  [[nodiscard]] R secret() const { 
    R res=values_[0];
    for (int i = 1; i < values_.size(); i++)
     res+=values_[i];
    return res;
  }
  // Arithmetic operators.
  TPShare<R>& operator+=(const TPShare<R>& rhs) {
    for (size_t i = 1; i < values_.size(); i++) {
      values_[i] += rhs.values_[i];
      tags_[i] += rhs.tag_[i];
    }
    return *this;
  }

  friend TPShare<R> operator+(TPShare<R> lhs,
                                      const TPShare<R>& rhs) {
    lhs += rhs;
    return lhs;
  }

  TPShare<R>& operator-=(const TPShare<R>& rhs) {
    (*this) += (rhs * -1);
    return *this;
  }

  friend TPShare<R> operator-(TPShare<R> lhs,
                                      const TPShare<R>& rhs) {
    lhs -= rhs;
    return lhs;
  }

  TPShare<R>& operator*=(const R& rhs) {
    for(size_t i = 1; i < values_.size(); i++) {
      values_[i] *= rhs;
      tags_[i] *= rhs;
    }
    return *this;
  }

  friend TPShare<R> operator*(TPShare<R> lhs, const R& rhs) {
    lhs *= rhs;
    return lhs;
  }

  AuthAddShare<R> getAAS(size_t pid){
    return AuthAddShare<R>({key_sh_.at(pid), values_.at(pid), tags_.at(pid)});
  }

  //Add above
  
};
//add the constructor above



// Contains all elements of a secret sharing. Used only for generating dummy
// preprocessing data.
/*
template <class R>
struct DummyShare { 
  // number of components will depent upon number of parties
  std::array<R, 6> share_elements;

  DummyShare() = default;

  explicit DummyShare(std::array<R, 6> share_elements)
      : share_elements(std::move(share_elements)) {}

  DummyShare(R secret, emp::PRG& prg) {
    prg.random_data(share_elements.data(), sizeof(R) * 5);

    R sum = share_elements[0];
    for (int i = 1; i < 5; ++i) {
      sum += share_elements[i];
    }
    share_elements[5] = secret - sum;
  }

  void randomize(emp::PRG& prg) {
    prg.random_data(share_elements.data(), sizeof(R) * 6);
  }

  [[nodiscard]] R secret() const {
    R sum = share_elements[0];
    for (size_t i = 1; i < 6; ++i) {
      sum += share_elements[i];
    }

    return sum;
  }

  DummyShare<R>& operator+=(const DummyShare<R>& rhs) {
    for (size_t i = 0; i < 6; ++i) {
      share_elements[i] += rhs.share_elements[i];
    }

    return *this;
  }

  friend DummyShare<R> operator+(DummyShare<R> lhs, const DummyShare<R>& rhs) {
    lhs += rhs;
    return lhs;
  }

  DummyShare<R>& operator-=(const DummyShare<R>& rhs) {
    for (size_t i = 0; i < 6; ++i) {
      share_elements[i] -= rhs.share_elements[i];
    }

    return *this;
  }

  friend DummyShare<R> operator-(DummyShare<R> lhs, const DummyShare<R>& rhs) {
    lhs -= rhs;
    return lhs;
  }

  DummyShare<R>& operator*=(const R& rhs) {
    for (size_t i = 0; i < 6; ++i) {
      share_elements[i] *= rhs;
    }

    return *this;
  }

  friend DummyShare<R> operator*(DummyShare<R> lhs, const R& rhs) {
    lhs *= rhs;
    return lhs;
  }

  friend DummyShare<R> operator*(const R& lhs, DummyShare<R> rhs) {
    // Assumes abelian ring.
    rhs *= lhs;
    return rhs;
  }

  //ReplicatedShare<R> getRSS(size_t pid) {
  //  return ReplicatedShare<R>({getShareElement(pid, pidFromOffset(pid, 1)),
  //                             getShareElement(pid, pidFromOffset(pid, 2)),
  //                             getShareElement(pid, pidFromOffset(pid, 3))});
  //}

  R getShareElement(size_t i, size_t j) {
    return share_elements.at(upperTriangularToArray(i, j));
  }
};*/

//template <>
//void AuthAddShare<BoolRing>::randomize(emp::PRG& prg);

//template <>
//TPShare<BoolRing>::TPShare(BoolRing secret, emp::PRG& prg);

//template <>
//void TPShare<BoolRing>::randomize(emp::PRG& prg);
};  // namespace dirigent
