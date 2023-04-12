#pragma once

#include <emp-tool/emp-tool.h>

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>

#include "../io/netmp.h"
#include "../utils/circuit.h"


#include "preproc.h"
#include "dirigent/rand_gen_pool.h"
#include "sharing.h"
#include "types.h"

namespace dirigent {
class OfflineEvaluator {
  int nP_;  
  int id_;
  int security_param_;
  RandGenPool rgen_;
  std::shared_ptr<io::NetIOMP> network_;
  quadsquad::utils::LevelOrderedCircuit circ_;
  std::shared_ptr<ThreadPool> tpool_;
  PreprocCircuit<Field> preproc_;
  
  


  // Used for running common coin protocol. Returns common random PRG key which
  // is then used to generate randomness for common coin output.
  //emp::block commonCoinKey();

 public:
  int party_num() { return nP_; }
  OfflineEvaluator(int nP, int my_id, std::shared_ptr<io::NetIOMP> network,
                   quadsquad::utils::LevelOrderedCircuit circ, int security_param,
                   int threads, int seed = 200);
  // Generate sharing of a random unknown value.
  static void randomShare(int nP, int pid, RandGenPool& rgen, io::NetIOMP& network,
                                   AuthAddShare<Field>& share, 
                                   TPShare<Field>& tpShare);
  // Generate sharing of a random value known to dealer (called by all parties
  // except the dealer).
  //static void randomShareWithParty(int pid, int dealer=0, RandGenPool& rgen,
  //                                 io::NetIOMP& network,
  //                                 ReplicatedShare<Field>& share);
  // Generate sharing of a random value known to party. Should be called by
  // dealer when other parties call other variant.
  static void randomShareWithParty(int nP, int pid, int dealer, RandGenPool& rgen,
                                           io::NetIOMP& network,
                                           AuthAddShare<Field>& share,
                                           TPShare<Field>& tpShare, 
                                           Field& secret);

  // Following methods implement various preprocessing subprotocols.

  // Set masks for each wire. Should be called before running any of the other
  // subprotocols.
  void setWireMasks(const std::unordered_map<quadsquad::utils::wire_t, int>& input_pid_map);
  
  
  PreprocCircuit<Field> getPreproc();

  // Efficiently runs above subprotocols.
  PreprocCircuit<Field> run(
      const std::unordered_map<quadsquad::utils::wire_t, int>& input_pid_map);

  
};
};  // namespace dirigent
