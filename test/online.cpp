#define BOOST_TEST_MODULE online
#include <emp-tool/emp-tool.h>
#include <io/netmp.h>
#include <dirigent/offline_evaluator.h>
#include <dirigent/online_evaluator.h>
#include <dirigent/sharing.h>

#include <boost/test/data/monomorphic.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/included/unit_test.hpp>
#include <cmath>
#include <future>
#include <memory>
#include <string>
#include <vector>
#include <thread>

using namespace dirigent;
using namespace common::utils;
namespace bdata = boost::unit_test::data;
constexpr int TEST_DATA_MAX_VAL = 1000;
constexpr int SECURITY_PARAM = 128;

BOOST_AUTO_TEST_SUITE(online_evaluator)

BOOST_AUTO_TEST_CASE(mult) {
  int nP = 4;
  auto seed_block = emp::makeBlock(0, 200);
  emp::PRG prg(&seed_block);
  std::mt19937 gen(200);
  std::uniform_int_distribution<Field> distrib(0, TEST_DATA_MAX_VAL);
  common::utils::Circuit<Field> circ;
  std::vector<common::utils::wire_t> input_wires;
  std::unordered_map<common::utils::wire_t, int> input_pid_map;
  std::unordered_map<common::utils::wire_t, Field> inputs;

  for (size_t i = 0; i < 2; ++i) {
    auto winp = circ.newInputWire();
    input_wires.push_back(winp);
    input_pid_map[winp] = 1;
    
    inputs[winp] = distrib(gen);
  }
  auto w_amb =
     circ.addGate(common::utils::GateType::kMul, input_wires[0], input_wires[1]);
  
  circ.setAsOutput(w_amb);
  auto level_circ = circ.orderGatesByLevel();
  auto exp_output = circ.evaluate(inputs);
  std::vector<std::future<std::vector<Field>>> parties;
  parties.reserve(nP+1);
  for (int i = 0; i <= nP; ++i) {
      parties.push_back(std::async(std::launch::async, [&, i, input_pid_map, inputs]() {
      
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      
      OfflineEvaluator eval(nP, i, network, 
                            level_circ, SECURITY_PARAM, 4);
      auto preproc = eval.run(input_pid_map);
     
      OnlineEvaluator online_eval(nP, i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);
      
      auto res = online_eval.evaluateCircuit(inputs);
      return res;
      
    }));
  }
  int i = 0;
  for (auto& p : parties) {
    auto output = p.get();
      if(i > 0) {
        BOOST_TEST(exp_output == output);
      }
      i++;
  }
  
}


BOOST_AUTO_TEST_CASE(EQZ_zero) {
  int nP = 4;
  auto seed_block = emp::makeBlock(0, 200);
  emp::PRG prg(&seed_block);
  std::mt19937 gen(200);
  std::uniform_int_distribution<Field> distrib(0, TEST_DATA_MAX_VAL);
  common::utils::Circuit<Field> circ;
  std::vector<common::utils::wire_t> input_wires;
  std::unordered_map<common::utils::wire_t, int> input_pid_map;
  std::unordered_map<common::utils::wire_t, Field> inputs;

  for (size_t i = 0; i < 2; ++i) {
    auto winp = circ.newInputWire();
    input_wires.push_back(winp);
    input_pid_map[winp] = 1;
    
    inputs[winp] = 0;
  }
  auto w_eqz =
     circ.addGate(common::utils::GateType::kEqz, input_wires[0]);
  
  circ.setAsOutput(w_eqz);
  auto level_circ = circ.orderGatesByLevel();
  auto exp_output = circ.evaluate(inputs);
  std::vector<std::future<std::vector<Field>>> parties;
  parties.reserve(nP+1);
  for (int i = 0; i <= nP; ++i) {
      parties.push_back(std::async(std::launch::async, [&, i, input_pid_map, inputs]() {
      
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      
      OfflineEvaluator eval(nP, i, network, 
                            level_circ, SECURITY_PARAM, 4);
      auto preproc = eval.run(input_pid_map);
     
      OnlineEvaluator online_eval(nP, i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);
      
      auto res = online_eval.evaluateCircuit(inputs);
      return res;
      
    }));
  }
  int i = 0;
  for (auto& p : parties) {
    auto output = p.get();
      if(i > 0) {
        BOOST_TEST(output == exp_output);
      }
      i++;
  }
  // std::cout<< "EQZ_zero completed succcessfully " << std::endl;
}


BOOST_AUTO_TEST_CASE(EQZ_non_zero) {
  int nP = 4;
  auto seed_block = emp::makeBlock(0, 200);
  emp::PRG prg(&seed_block);
  std::mt19937 gen(200);
  std::uniform_int_distribution<Field> distrib(0, TEST_DATA_MAX_VAL);
  common::utils::Circuit<Field> circ;
  std::vector<common::utils::wire_t> input_wires;
  std::unordered_map<common::utils::wire_t, int> input_pid_map;
  std::unordered_map<common::utils::wire_t, Field> inputs;

  for (size_t i = 0; i < 2; ++i) {
    auto winp = circ.newInputWire();
    input_wires.push_back(winp);
    input_pid_map[winp] = 1;
    inputs[winp] = 30234235;
  }
  auto w_eqz =
     circ.addGate(common::utils::GateType::kEqz, input_wires[0]);
  
  circ.setAsOutput(w_eqz);
  auto level_circ = circ.orderGatesByLevel();
  auto exp_output = circ.evaluate(inputs);
  std::vector<std::future<std::vector<Field>>> parties;
  parties.reserve(nP+1);
  for (int i = 0; i <= nP; ++i) {
      parties.push_back(std::async(std::launch::async, [&, i, input_pid_map, inputs]() {
      
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      
      OfflineEvaluator eval(nP, i, network, 
                            level_circ, SECURITY_PARAM, 4);
      auto preproc = eval.run(input_pid_map);
     
      OnlineEvaluator online_eval(nP, i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);
      
      auto res = online_eval.evaluateCircuit(inputs);
      // std::cout<< "party id = " << i <<std::endl;
      return res;
      
    }));
  }
  int i = 0;
  for (auto& p : parties) {
    auto output = p.get();
      if(i > 0) {
        // BOOST_TEST(output[0] == 1);
        BOOST_TEST(output == exp_output);
      }
      i++;
  }
  // std::cout<< "EQZ_non_zero completed succcessfully " << std::endl;

}


BOOST_AUTO_TEST_CASE(LTZ) {
  int nP = 4;
  auto seed_block = emp::makeBlock(0, 200);
  emp::PRG prg(&seed_block);
  std::mt19937 gen(200);
  std::uniform_int_distribution<Field> distrib(0, TEST_DATA_MAX_VAL);
  common::utils::Circuit<Field> circ;
  std::vector<common::utils::wire_t> input_wires;
  std::unordered_map<common::utils::wire_t, int> input_pid_map;
  std::unordered_map<common::utils::wire_t, Field> inputs;

  for (size_t i = 0; i < 2; ++i) {
    auto winp = circ.newInputWire();
    input_wires.push_back(winp);
    input_pid_map[winp] = 1;
    
    inputs[winp] = distrib(gen);
    // std::cout<< inputs[winp];
    // inputs[winp] = 4;
    // inputs[winp] = -4;
  }
  inputs[0] = 4;
  inputs[1] = -4;
  auto w_ltz =
     circ.addGate(common::utils::GateType::kLtz, input_wires[0]);
  auto w_ltz_b =
     circ.addGate(common::utils::GateType::kLtz, input_wires[1]);
  circ.setAsOutput(w_ltz);
  circ.setAsOutput(w_ltz_b);
  auto level_circ = circ.orderGatesByLevel();
  auto exp_output = circ.evaluate(inputs);
  std::vector<std::future<std::vector<Field>>> parties;
  parties.reserve(nP+1);
  for (int i = 0; i <= nP; ++i) {
      parties.push_back(std::async(std::launch::async, [&, i, input_pid_map, inputs]() {
      
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      
      OfflineEvaluator eval(nP, i, network, 
                            level_circ, SECURITY_PARAM, 4);
      auto preproc = eval.run(input_pid_map);
     
      OnlineEvaluator online_eval(nP, i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);
      
      auto res = online_eval.evaluateCircuit(inputs);
      return res;
      
    }));
  }
  int i = 0;
  for (auto& p : parties) {
    auto output = p.get();
      if(i > 0) {
        BOOST_TEST(output == exp_output);
        std::cout<< output[0] << std::endl;
        std::cout<< output[1] << std::endl;
      }
      i++;
  }
}


BOOST_AUTO_TEST_CASE(depth_2_circuit) {
  int nP = 10;
  auto seed_block = emp::makeBlock(0, 200);
  emp::PRG prg(&seed_block);
  std::mt19937 gen(200);
  std::uniform_int_distribution<Field> distrib(0, TEST_DATA_MAX_VAL);
  common::utils::Circuit<Field> circ;
  std::vector<common::utils::wire_t> input_wires;
  std::unordered_map<common::utils::wire_t, int> input_pid_map;
  std::unordered_map<common::utils::wire_t, Field> inputs;

  for (size_t i = 0; i < 4; ++i) {
    auto winp = circ.newInputWire();
    input_wires.push_back(winp);
    input_pid_map[winp] = 1;
    
    inputs[winp] = distrib(gen);
  }
  auto w_aab =
     circ.addGate(common::utils::GateType::kAdd, input_wires[0], input_wires[1]);
  auto w_cmd =
     circ.addGate(common::utils::GateType::kMul, input_wires[2], input_wires[3]);
  auto w_mout = circ.addGate(common::utils::GateType::kMul, w_aab, w_cmd);
  auto w_aout = circ.addGate(common::utils::GateType::kAdd, w_aab, w_cmd);
   circ.setAsOutput(w_cmd);
   circ.setAsOutput(w_mout);
   circ.setAsOutput(w_aout);
  auto level_circ = circ.orderGatesByLevel();
  auto exp_output = circ.evaluate(inputs);
  std::vector<std::future<std::vector<Field>>> parties;
  parties.reserve(nP+1);
  for (int i = 0; i <= nP; ++i) {
      parties.push_back(std::async(std::launch::async, [&, i, input_pid_map, inputs]() {
      
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      
      OfflineEvaluator eval(nP, i, network, 
                            level_circ, SECURITY_PARAM, 4);
      auto preproc = eval.run(input_pid_map);
     
      OnlineEvaluator online_eval(nP, i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);
      
      auto res = online_eval.evaluateCircuit(inputs);
      return res;
      
    }));
  }
  int i = 0;  
  for (auto& p : parties) {
    auto output = p.get();
      if(i > 0) {
        BOOST_TEST(exp_output == output);
      }
      i++;
  }
  
}



BOOST_AUTO_TEST_CASE(dotp_gate) {
  auto seed = emp::makeBlock(100, 200);
  int nf = 10;
  int nP = 5;
  Circuit<Field> circ;
  std::vector<wire_t> vwa(nf);
  std::vector<wire_t> vwb(nf);
  for (int i = 0; i < nf; i++) {
    vwa[i] = circ.newInputWire();
    vwb[i] = circ.newInputWire();
  }
  auto wdotp = circ.addGate(GateType::kDotprod, vwa, vwb);
  circ.setAsOutput(wdotp);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, Field> input_map;
  std::unordered_map<wire_t, int> input_pid_map;
  std::mt19937 gen(200);
  std::uniform_int_distribution<Ring> distrib(0, TEST_DATA_MAX_VAL);
  for (size_t i = 0; i < nf; ++i) {
    input_map[vwa[i]] = distrib(gen);
    input_map[vwb[i]] = distrib(gen);
    input_pid_map[vwa[i]] = 1;
    input_pid_map[vwb[i]] = 2;
  }

  auto exp_output = circ.evaluate(input_map);

  std::vector<std::future<std::vector<Field>>> parties;
  //std::vector<std::future<PreprocCircuit<Field>>> parties;
  parties.reserve(nP+1);
  for (int i = 0; i <= nP; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i, input_pid_map, input_map]() {
      
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      
      OfflineEvaluator eval(nP, i, network, 
                            level_circ, SECURITY_PARAM, 4);

      auto preproc = eval.run(input_pid_map);
      //return preproc;
      OnlineEvaluator online_eval(nP, i, std::move(network), std::move(preproc),
                                   level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(input_map);
    }));
  }
  int i = 0;
  for (auto& p : parties) {
    if(i > 0) {
      auto output = p.get();
      BOOST_TEST(output == exp_output);
    }
    i++;
  }
}


BOOST_AUTO_TEST_CASE(mult3) {
  int nP = 4;
  auto seed_block = emp::makeBlock(0, 200);
  emp::PRG prg(&seed_block);
  std::mt19937 gen(200);
  std::uniform_int_distribution<Field> distrib(0, TEST_DATA_MAX_VAL);
  common::utils::Circuit<Field> circ;
  std::vector<common::utils::wire_t> input_wires;
  std::unordered_map<common::utils::wire_t, int> input_pid_map;
  std::unordered_map<common::utils::wire_t, Field> inputs;

  for (size_t i = 0; i < 4; ++i) {
    auto winp = circ.newInputWire();
    input_wires.push_back(winp);
    input_pid_map[winp] = 1;
    
    inputs[winp] = distrib(gen);
    
  }
  auto w_aab =
     circ.addGate(common::utils::GateType::kAdd, input_wires[0], input_wires[1]);
  auto w_cmd =
      circ.addGate(common::utils::GateType::kMul, input_wires[2], input_wires[3]);
  auto w_mout = circ.addGate(common::utils::GateType::kMul, w_aab, w_cmd);
  auto w_aout = circ.addGate(common::utils::GateType::kAdd, w_aab, w_cmd);
  auto w_mul_th = circ.addGate(common::utils::GateType::kMul3, w_aab, w_cmd, w_mout);
   circ.setAsOutput(w_cmd);
   circ.setAsOutput(w_mout);
   circ.setAsOutput(w_aout);
   circ.setAsOutput(w_mul_th);

  auto level_circ = circ.orderGatesByLevel();
  auto exp_output = circ.evaluate(inputs);
  
  std::vector<std::future<std::vector<Field>>> parties;
  parties.reserve(nP+1);
  for (int i = 0; i <= nP; ++i) {
      parties.push_back(std::async(std::launch::async, [&, i, input_pid_map, inputs]() {
      
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      
      OfflineEvaluator eval(nP, i, network, 
                            level_circ, SECURITY_PARAM, 4);
      auto preproc = eval.run(input_pid_map);
     
      OnlineEvaluator online_eval(nP, i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);
      
      auto res = online_eval.evaluateCircuit(inputs);
      return res;
      
    }));
  }
  int i = 0;
  for (auto& p : parties) {
    auto output = p.get();
      if(i > 0) {
         BOOST_TEST(exp_output == output);
      }
      i++;
  }
  
}


// BOOST_AUTO_TEST_CASE(mult4) {
//   int nP = 4;
//   auto seed_block = emp::makeBlock(0, 200);
//   emp::PRG prg(&seed_block);
//   std::mt19937 gen(200);
//   std::uniform_int_distribution<Field> distrib(0, TEST_DATA_MAX_VAL);
//   common::utils::Circuit<Field> circ;
//   std::vector<common::utils::wire_t> input_wires;
//   std::unordered_map<common::utils::wire_t, int> input_pid_map;
//   std::unordered_map<common::utils::wire_t, Field> inputs;

//   for (size_t i = 0; i < 4; ++i) {
//     auto winp = circ.newInputWire();
//     input_wires.push_back(winp);
//     input_pid_map[winp] = 1;
    
//     // inputs[winp] = distrib(gen);
//     inputs[winp] = 1;
//   }
//   auto w_aab =
//      circ.addGate(common::utils::GateType::kAdd, input_wires[0], input_wires[1]);
//   auto w_cmd =
//       circ.addGate(common::utils::GateType::kMul, input_wires[2], input_wires[3]);
//   auto w_mout = circ.addGate(common::utils::GateType::kMul, w_aab, w_cmd);
//   auto w_aout = circ.addGate(common::utils::GateType::kAdd, w_aab, w_cmd);
//   auto w_mul_f = circ.addGate(common::utils::GateType::kMul4, w_mout, w_aout, w_cmd, w_aab);
//   auto w_mul_d = circ.addGate(common::utils::GateType::kMul4, w_aout, w_cmd, w_aab, w_mul_f);
//   // auto w_mul_f = circ.addGate(common::utils::GateType::kMul4, w_aab, w_cmd, w_mout, w_aout);
//   // auto w_mul_d = circ.addGate(common::utils::GateType::kMul4, w_mout, w_aout, w_cmd, w_mul_f);
//    circ.setAsOutput(w_cmd);
//    circ.setAsOutput(w_mout);
//    circ.setAsOutput(w_aout);
//    circ.setAsOutput(w_mul_f);
//    circ.setAsOutput(w_mul_d);

//   auto level_circ = circ.orderGatesByLevel();
//   auto exp_output = circ.evaluate(inputs);
  
//   std::vector<std::future<std::vector<Field>>> parties;
//   parties.reserve(nP+1);
//   for (int i = 0; i <= nP; ++i) {
//       parties.push_back(std::async(std::launch::async, [&, i, input_pid_map, inputs]() {
      
//       auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      
//       OfflineEvaluator eval(nP, i, network, 
//                             level_circ, SECURITY_PARAM, 4);
//       auto preproc = eval.run(input_pid_map);
     
//       OnlineEvaluator online_eval(nP, i, std::move(network), std::move(preproc),
//                                   level_circ, SECURITY_PARAM, 1);
      
//       auto res = online_eval.evaluateCircuit(inputs);
//       return res;
      
//     }));
//   }
//   int i = 0;
//   for (auto& p : parties) {
//     auto output = p.get();
//       if(i > 0) {
//         BOOST_TEST(exp_output == output);

//         std::cout<<"exp_output = " << exp_output[exp_output.size() - 1] << " output = " << output[output.size() - 1] << std::endl;
//       }
//       i++;
//   }

// }

BOOST_AUTO_TEST_CASE(mult4_2) {
  int nP = 4;
  auto seed_block = emp::makeBlock(0, 200);
  emp::PRG prg(&seed_block);
  std::mt19937 gen(200);
  std::uniform_int_distribution<Field> distrib(0, TEST_DATA_MAX_VAL);
  common::utils::Circuit<Field> circ;
  std::vector<common::utils::wire_t> input_wires;
  std::unordered_map<common::utils::wire_t, int> input_pid_map;
  std::unordered_map<common::utils::wire_t, Field> inputs;

  for (size_t i = 0; i < 8; ++i) {
    auto winp = circ.newInputWire();
    input_wires.push_back(winp);
    input_pid_map[winp] = 1;
    
    // inputs[winp] = distrib(gen);
    inputs[winp] = 1;
  }

  auto w_m1 = circ.addGate(common::utils::GateType::kMul, input_wires[0], input_wires[1]);
  auto w_m3_2 = circ.addGate(common::utils::GateType::kMul3, input_wires[2], input_wires[3], w_m1);
  auto w_m3_3 = circ.addGate(common::utils::GateType::kMul3, input_wires[4], input_wires[5], w_m1);
  auto w_m4_1 = circ.addGate(common::utils::GateType::kMul4, input_wires[6], input_wires[7], w_m1, w_m3_2);
  auto w_m4_2 = circ.addGate(common::utils::GateType::kMul4, input_wires[6], input_wires[7], w_m1, w_m3_3);
  auto w_m2 = circ.addGate(common::utils::GateType::kMul, input_wires[0], w_m4_2);
  circ.setAsOutput(w_m1);
  circ.setAsOutput(w_m4_1);
  circ.setAsOutput(w_m2);

  auto level_circ = circ.orderGatesByLevel();
  auto exp_output = circ.evaluate(inputs);
  
  std::vector<std::future<std::vector<Field>>> parties;
  parties.reserve(nP+1);
  for (int i = 0; i <= nP; ++i) {
      parties.push_back(std::async(std::launch::async, [&, i, input_pid_map, inputs]() {
      
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      
      OfflineEvaluator eval(nP, i, network, 
                            level_circ, SECURITY_PARAM, 4);
      auto preproc = eval.run(input_pid_map);
     
      OnlineEvaluator online_eval(nP, i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);
      
      auto res = online_eval.evaluateCircuit(inputs);
      return res;
      
    }));
  }
  int i = 0;
  for (auto& p : parties) {
    auto output = p.get();
      if(i > 0) {
        BOOST_TEST(exp_output == output);

        // std::cout<<"exp_output = " << exp_output[exp_output.size() - 1] << " output = " << output[output.size() - 1] << std::endl;
      }
      i++;
  }

}
BOOST_AUTO_TEST_SUITE_END()

// BOOST_AUTO_TEST_CASE(Multk) {
//   int nP = 5;
//   common::utils::Circuit<Field> circ = common::utils::Circuit<Field>::generateMultK();
//   std::unordered_map<common::utils::wire_t, int> input_pid_map;
//   std::unordered_map<common::utils::wire_t, Field> inputs;
//   for(size_t i = 0; i <= 64; i++) {
//     input_pid_map[i] = 1;
//     inputs[i] = 1;
//   }
//   auto level_circ = circ.orderGatesByLevel();
//   std::vector<std::future<std::vector<Field>>> parties;
//   parties.reserve(nP+1);
//   for (int i = 0; i <= nP; ++i) {
//       parties.push_back(std::async(std::launch::async, [&, i, input_pid_map, inputs]() {
      
//       auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      
//       OfflineEvaluator eval(nP, i, network, level_circ, SECURITY_PARAM, 1);
//       std::cout<<"OFFLINE STARTED" <<std::endl;
//       auto preproc = eval.run(input_pid_map);
//       std::cout<<"OFFLINE COMPLETED" <<std::endl;
//       OnlineEvaluator online_eval(nP, i, std::move(network), std::move(preproc),
//                                   level_circ, SECURITY_PARAM, 1);
      
//           auto res =  online_eval.evaluateCircuit(inputs);
            
//       // auto res = online_eval.evaluateCircuit(inputs);
//       return res;
//     }));
//   }
// }

// BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(online_bool_evaluator)

BOOST_AUTO_TEST_CASE(Multk_bool) {
  int nP = 5;
  common::utils::Circuit<BoolRing> circ = common::utils::Circuit<BoolRing>::generateMultK();
  std::unordered_map<common::utils::wire_t, int> input_pid_map(64);
  std::unordered_map<common::utils::wire_t, BoolRing> input_map(64);
  std::unordered_map<common::utils::wire_t, BoolRing> bit_mask_map(64);
  std::vector<AuthAddShare<BoolRing>> output_mask;
  std::vector<TPShare<BoolRing>>   output_tpmask;
  // BoolRing exp_output = 1;
   for (size_t i = 0; i < 64; ++i) {
    input_map[i] = 1;
    input_pid_map[i] = 1;
    bit_mask_map[i] = 0;
    // exp_output *= input_map[i];
  }
  auto level_circ = circ.orderGatesByLevel();
  auto exp_output = circ.evaluate(input_map);
  std::vector<std::future<std::vector<BoolRing>>> parties;
  parties.reserve(nP+1);
  for (int i = 0; i <= nP; ++i) {
      parties.push_back(std::async(std::launch::async, [&, i, input_pid_map, input_map]() {
      
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      
      OfflineBoolEvaluator eval(nP, i, network, level_circ);
      
      auto preproc = eval.run(input_pid_map, bit_mask_map, output_mask, output_tpmask);
      
      BoolEvaluator online_eval(nP, i, std::move(network), std::move(preproc),
                                  level_circ);
      
      auto res =  online_eval.evaluateCircuit(input_map);
      return res;
    }));
  }

  int i = 0;
  for (auto& p : parties) {
    auto output = p.get();
      if(i > 0) {
        BOOST_TEST(exp_output == output);
      }
      i++;
  }
}



BOOST_AUTO_TEST_CASE(PrefixAND) {
  int nP = 4;
  common::utils::Circuit<BoolRing> circ = common::utils::Circuit<BoolRing>::generatePrefixAND();
  std::unordered_map<common::utils::wire_t, int> input_pid_map;
  std::unordered_map<common::utils::wire_t, BoolRing> inputs;
  std::unordered_map<common::utils::wire_t, BoolRing> bit_mask_map;
  std::vector<AuthAddShare<BoolRing>> output_mask;
  std::vector<TPShare<BoolRing>>   output_tpmask;
  for(size_t i = 0; i < 2 * 64; i++) {
    input_pid_map[i] = 1;
    bit_mask_map[i] = 0; 
    inputs[i] = 1;
  }
  inputs[60] = 0;
  inputs[62] = 0; 
  std::vector<BoolRing> prod(64);
  for(size_t i = 0; i < 64; i++) {
    inputs[i + 64] = rand() % 2;
    prod[i] = inputs[0];
    for(size_t j = 0; j <= i; j++) {
      prod[i] *= inputs[j];
    }
  }
  auto level_circ = circ.orderGatesByLevel();
  auto exp_output = circ.evaluate(inputs);
  auto exp_out = inputs[124];
  std::vector<std::future<std::vector<BoolRing>>> parties;
  parties.reserve(nP+1);
  for (int i = 0; i <= nP; ++i) {
      parties.push_back(std::async(std::launch::async, [&, i, input_pid_map, inputs]() {
      
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      
      OfflineBoolEvaluator eval(nP, i, network, level_circ);
      auto preproc = eval.run(input_pid_map, bit_mask_map, output_mask, output_tpmask);
      BoolEvaluator online_eval(nP, i, std::move(network), std::move(preproc),
                                  level_circ);
      
      auto res =  online_eval.evaluateCircuit(inputs);
      return res;
    }));
  }
  int i = 0;
  for (auto& p : parties) {
    auto output = p.get();
      if(i > 0) {
        BOOST_TEST(exp_output == output);
      }
      i++;
  }
}

BOOST_AUTO_TEST_CASE(ParaPrefixAND) {
  int nP = 4;
  int repeat =2;
  int k = 64;
  common::utils::Circuit<BoolRing> circ = common::utils::Circuit<BoolRing>::generateParaPrefixAND(repeat);
  std::unordered_map<common::utils::wire_t, int> input_pid_map;
  std::unordered_map<common::utils::wire_t, BoolRing> inputs;
  std::unordered_map<common::utils::wire_t, BoolRing> bit_mask_map;
  std::vector<AuthAddShare<BoolRing>> output_mask;
  std::vector<TPShare<BoolRing>>   output_tpmask;
  for (int rep = 0; rep < repeat; rep++) {
    for(size_t i = 0; i <= k; i++) {
      input_pid_map[(rep*(k+1)) + i] = 1;
      inputs[(rep*(k+1)) + i] = 1;
      bit_mask_map[(rep*(k+1)) + i] = 0; 
    }
  }
  auto level_circ = circ.orderGatesByLevel();
  std::vector<std::future<std::vector<BoolRing>>> parties;
  parties.reserve(nP+1);
  for (int i = 0; i <= nP; ++i) {
      parties.push_back(std::async(std::launch::async, [&, i, input_pid_map, inputs]() {
      
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      
      OfflineBoolEvaluator eval(nP, i, network, level_circ);
      auto preproc = eval.run(input_pid_map, bit_mask_map, output_mask, output_tpmask);
      BoolEvaluator online_eval(nP, i, std::move(network), std::move(preproc),
                                  level_circ);
      
          auto res =  online_eval.evaluateCircuit(inputs);
            
      // auto res = online_eval.evaluateCircuit(inputs);
      return res;
    }));
  }
}

BOOST_AUTO_TEST_SUITE_END()
//   int nP = 4;
//   common::utils::Circuit<BoolRing> circ;
//   auto wa = circ.newInputWire();
//   auto wb = circ.newInputWire();
//   auto wprod = circ.addGate(GateType::kMul, wa, wb);
//   circ.setAsOutput(wprod);
//   BoolRing a(0);
//   BoolRing b(1);
//   auto output = circ.evaluate({{wa, a}, {wb, b}});
  // std::vector<common::utils::wire_t> input_wires;
  // std::unordered_map<common::utils::wire_t, int> input_pid_map;
  // std::unordered_map<common::utils::wire_t, BoolRing> inputs;

  // for (size_t i = 0; i < 2; ++i) {
  //   auto winp = circ.newInputWire();
  //   input_wires.push_back(winp);
  //   input_pid_map[winp] = 1;
    
  //   inputs[winp] = 1;
  // }
  // auto w_amb =
  //    circ.addGate(common::utils::GateType::kMul, input_wires[0], input_wires[1]);
  // circ.setAsOutput(w_amb);
  // auto level_circ = circ.orderGatesByLevel();

  // auto exp_output = circ.evaluate(inputs);
// }
//   int nP = 4;
//   auto seed_block = emp::makeBlock(0, 200);
//   emp::PRG prg(&seed_block);
//   std::mt19937 gen(200);
//   std::uniform_int_distribution<BoolRing> distrib(0, TEST_DATA_MAX_VAL);
//   common::utils::Circuit<BoolRing> circ;
//   std::vector<common::utils::wire_t> input_wires;
//   std::unordered_map<common::utils::wire_t, int> input_pid_map;
//   std::unordered_map<common::utils::wire_t, Field> inputs;

//   for (size_t i = 0; i < 2; ++i) {
//     auto winp = circ.newInputWire();
//     input_wires.push_back(winp);
//     input_pid_map[winp] = 1;
    
//     inputs[winp] = 1;
//   }
//   auto w_amb =
//      circ.addGate(common::utils::GateType::kMul, input_wires[0], input_wires[1]);
//   // auto w_cmd =
//   //     circ.addGate(common::utils::GateType::kMul, input_wires[2], input_wires[3]);
//   // auto w_mout = circ.addGate(common::utils::GateType::kMul, w_aab, w_cmd);
//   // auto w_aout = circ.addGate(common::utils::GateType::kAdd, w_aab, w_cmd);
//   //  circ.setAsOutput(w_cmd);
//   //  circ.setAsOutput(w_mout);
//   //  circ.setAsOutput(w_aout);
//   circ.setAsOutput(w_amb);
//   auto level_circ = circ.orderGatesByLevel();
//   auto exp_output = circ.evaluate(inputs);
//   std::vector<std::future<std::vector<BoolRing>>> parties;
//   parties.reserve(nP+1);
//   for (int i = 0; i <= nP; ++i) {
//       parties.push_back(std::async(std::launch::async, [&, i, input_pid_map, inputs]() {
      
//       auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      
//       OfflineBoolEvaluator eval(nP, i, network, 
//                             level_circ);
//       auto preproc = eval.run(input_pid_map);
     
//       BoolEvaluator online_eval(nP, i, std::move(network), std::move(preproc),
//                                   level_circ);
      
//       auto res = online_eval.evaluateCircuit(inputs);
//       return res;
      
//     }));
//   }
//   int i = 0;
//   for (auto& p : parties) {
//     auto output = p.get();
//       if(i > 0) {
//         BOOST_TEST(exp_output == output);
//       }
//       i++;
//   }
  
// }

// BOOST_AUTO_TEST_SUITE_END()

/*
BOOST_DATA_TEST_CASE(no_op_circuit,
                     bdata::random(0, TEST_DATA_MAX_VAL) ^ bdata::xrange(1),
                     input, idx) {
  auto seed = emp::makeBlock(100, 200);

  Circuit<Ring> circ;
  auto wa = circ.newInputWire();
  circ.setAsOutput(wa);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}};
  std::unordered_map<wire_t, Ring> inputs = {{wa, input}};
  auto exp_output = circ.evaluate(inputs);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < 4; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<4>>(i, 10000, nullptr, true);
      emp::PRG prg(&seed, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(inputs);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output == exp_output);
  }
}

BOOST_DATA_TEST_CASE(add_gate,
                     bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^ bdata::xrange(1),
                     input_a, input_b, idx) {
  auto seed = emp::makeBlock(100, 200);

  Circuit<Ring> circ;
  auto wa = circ.newInputWire();
  auto wb = circ.newInputWire();
  auto wsum = circ.addGate(GateType::kAdd, wa, wb);
  circ.setAsOutput(wsum);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}, {wb, 1}};
  std::unordered_map<wire_t, Ring> inputs = {{wa, input_a}, {wb, input_b}};
  auto exp_output = circ.evaluate(inputs);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < 4; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<4>>(i, 10000, nullptr, true);
      emp::PRG prg(&seed, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(inputs);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output == exp_output);
  }
}

BOOST_DATA_TEST_CASE(sub_gate,
                     bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^ bdata::xrange(1),
                     input_a, input_b, idx) {
  auto seed = emp::makeBlock(100, 200);

  Circuit<Ring> circ;
  auto wa = circ.newInputWire();
  auto wb = circ.newInputWire();
  auto wdiff = circ.addGate(GateType::kSub, wa, wb);
  circ.setAsOutput(wdiff);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}, {wb, 1}};
  std::unordered_map<wire_t, Ring> inputs = {{wa, input_a}, {wb, input_b}};
  auto exp_output = circ.evaluate(inputs);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < 4; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<4>>(i, 10000, nullptr, true);
      emp::PRG prg(&seed, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(inputs);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output == exp_output);
  }
}

BOOST_DATA_TEST_CASE(mul_gate,
                     bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^ bdata::xrange(1),
                     input_a, input_b, idx) {
  auto seed = emp::makeBlock(100, 200);

  Circuit<Ring> circ;
  auto wa = circ.newInputWire();
  auto wb = circ.newInputWire();
  auto wprod = circ.addGate(GateType::kMul, wa, wb);
  circ.setAsOutput(wprod);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}, {wb, 1}};
  std::unordered_map<wire_t, Ring> inputs = {{wa, input_a}, {wb, input_b}};
  auto exp_output = circ.evaluate(inputs);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < 4; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<4>>(i, 10000, nullptr, true);
      emp::PRG prg(&seed, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(inputs);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output == exp_output);
  }
}

BOOST_DATA_TEST_CASE(relu_gate, bdata::xrange(2), idx) {
  Circuit<Ring> circ;
  uint64_t a = 5;

  if (idx == 1) {
    a *= -1;
  }

  Ring input_a = static_cast<Ring>(a);
  std::vector<BoolRing> bits = bitDecompose(input_a);

  auto wa = circ.newInputWire();
  auto wrelu = circ.addGate(GateType::kRelu, wa);
  circ.setAsOutput(wrelu);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}};
  std::unordered_map<wire_t, Ring> inputs = {{wa, input_a}};
  auto exp_output = circ.evaluate(inputs);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < 4; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<4>>(i, 10000, nullptr, true);
      emp::PRG prg(&emp::zero_block, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(inputs);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output[0] == exp_output[0]);
  }
}

BOOST_DATA_TEST_CASE(msb_gate, bdata::xrange(2), idx) {
  Circuit<Ring> circ;
  uint64_t a = 5;

  if (idx == 1) {
    a *= -1;
  }

  Ring input_a = static_cast<Ring>(a);
  std::vector<BoolRing> bits = bitDecompose(input_a);

  auto wa = circ.newInputWire();
  auto wmsb = circ.addGate(GateType::kMsb, wa);
  circ.setAsOutput(wmsb);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}};
  std::unordered_map<wire_t, Ring> inputs = {{wa, input_a}};
  auto exp_output = circ.evaluate(inputs);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < 4; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<4>>(i, 10000, nullptr, true);
      emp::PRG prg(&emp::zero_block, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(inputs);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output[0] == exp_output[0]);
  }
}

BOOST_AUTO_TEST_CASE(double_relu_gate) {
  Circuit<Ring> circ;
  uint64_t a, b;
  a = 5;
  b = -23;
  Ring input_a = static_cast<Ring>(a);
  Ring input_b = b;

  auto wa = circ.newInputWire();
  auto wb = circ.newInputWire();
  auto wrelu_a = circ.addGate(GateType::kRelu, wa);
  auto wprod = circ.addGate(GateType::kMul, wrelu_a, wb);
  auto wrelu_prod = circ.addGate(GateType::kRelu, wprod);

  circ.setAsOutput(wrelu_a);
  circ.setAsOutput(wrelu_prod);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}, {wb, 1}};
  std::unordered_map<wire_t, Ring> inputs = {{wa, input_a}, {wb, input_b}};
  auto exp_output = circ.evaluate(inputs);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < 4; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<4>>(i, 10000, nullptr, true);
      emp::PRG prg(&emp::zero_block, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(inputs);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output[0] == exp_output[0]);
    BOOST_TEST(output[1] == exp_output[1]);
  }
}

BOOST_AUTO_TEST_CASE(dotp_gate) {
  auto seed = emp::makeBlock(100, 200);
  int nf = 10;

  Circuit<Ring> circ;
  std::vector<wire_t> vwa(nf);
  std::vector<wire_t> vwb(nf);
  for (int i = 0; i < nf; i++) {
    vwa[i] = circ.newInputWire();
    vwb[i] = circ.newInputWire();
  }
  auto wdotp = circ.addGate(GateType::kDotprod, vwa, vwb);
  circ.setAsOutput(wdotp);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, Ring> input_map;
  std::unordered_map<wire_t, int> input_pid_map;
  std::mt19937 gen(200);
  std::uniform_int_distribution<Ring> distrib(0, TEST_DATA_MAX_VAL);
  for (size_t i = 0; i < nf; ++i) {
    input_map[vwa[i]] = distrib(gen);
    input_map[vwb[i]] = distrib(gen);
    input_pid_map[vwa[i]] = 0;
    input_pid_map[vwb[i]] = 1;
  }

  auto exp_output = circ.evaluate(input_map);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < 4; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<4>>(i, 10000, nullptr, true);
      emp::PRG prg(&seed, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(input_map);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output == exp_output);
  }
}

BOOST_AUTO_TEST_CASE(tr_dotp_gate) {
  auto seed = emp::makeBlock(100, 200);
  int nf = 10;

  Circuit<Ring> circ;
  std::vector<wire_t> vwa(nf);
  std::vector<wire_t> vwb(nf);
  for (int i = 0; i < nf; i++) {
    vwa[i] = circ.newInputWire();
    vwb[i] = circ.newInputWire();
  }
  auto wdotp = circ.addGate(GateType::kTrdotp, vwa, vwb);
  circ.setAsOutput(wdotp);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, Ring> input_map;
  std::unordered_map<wire_t, int> input_pid_map;
  std::mt19937 gen(200);
  std::uniform_int_distribution<Ring> distrib(0, TEST_DATA_MAX_VAL);
  for (size_t i = 0; i < nf; ++i) {
    input_map[vwa[i]] = distrib(gen);
    input_map[vwb[i]] = distrib(gen);
    input_pid_map[vwa[i]] = 0;
    input_pid_map[vwb[i]] = 1;
  }

  auto exp_output = circ.evaluate(input_map);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < 4; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<4>>(i, 10000, nullptr, true);
      emp::PRG prg(&seed, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(input_map);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    bool check = (output[0] == exp_output[0]) ||
                 (output[0] == (exp_output[0] + 1)) ||
                 (output[0] == (exp_output[0] - 1));
    BOOST_TEST(check);
  }
}

BOOST_DATA_TEST_CASE(depth_2_circuit,
                     bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^ bdata::xrange(1),
                     input_a, input_b, input_c, input_d, idx) {
  auto seed = emp::makeBlock(100, 200);
  std::vector<int> vinputs = {input_a, input_b, input_c, input_d};

  Circuit<Ring> circ;
  std::vector<wire_t> input_wires;
  for (size_t i = 0; i < vinputs.size(); ++i) {
    input_wires.push_back(circ.newInputWire());
  }
  auto w_aab = circ.addGate(GateType::kAdd, input_wires[0], input_wires[1]);
  auto w_cmd = circ.addGate(GateType::kMul, input_wires[2], input_wires[3]);
  auto w_mout = circ.addGate(GateType::kMul, w_aab, w_cmd);
  auto w_aout = circ.addGate(GateType::kAdd, w_aab, w_cmd);
  circ.setAsOutput(w_mout);
  circ.setAsOutput(w_aout);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, int> input_pid_map;
  std::unordered_map<wire_t, Ring> inputs;
  for (size_t i = 0; i < vinputs.size(); ++i) {
    input_pid_map[input_wires[i]] = i % 4;
    inputs[input_wires[i]] = vinputs[i];
  }
  auto exp_output = circ.evaluate(inputs);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < 4; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<4>>(i, 10000, nullptr, true);
      emp::PRG prg(&seed, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(inputs);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output == exp_output);
  }
}

BOOST_DATA_TEST_CASE(multiple_inputs_same_party,
                     bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^ bdata::xrange(1),
                     input_a, input_b, input_c, input_d, idx) {
  auto seed = emp::makeBlock(100, 200);
  std::vector<int> vinputs = {input_a, input_b, input_c, input_d};

  Circuit<Ring> circ;
  std::vector<wire_t> input_wires;
  for (size_t i = 0; i < vinputs.size(); ++i) {
    input_wires.push_back(circ.newInputWire());
  }
  auto w_aab = circ.addGate(GateType::kAdd, input_wires[0], input_wires[1]);
  auto w_cmd = circ.addGate(GateType::kMul, input_wires[2], input_wires[3]);
  auto w_mout = circ.addGate(GateType::kMul, w_aab, w_cmd);
  auto w_aout = circ.addGate(GateType::kAdd, w_aab, w_cmd);
  circ.setAsOutput(w_mout);
  circ.setAsOutput(w_aout);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, int> input_pid_map;
  std::unordered_map<wire_t, Ring> inputs;
  for (size_t i = 0; i < vinputs.size(); ++i) {
    input_pid_map[input_wires[i]] = i % 2;
    inputs[input_wires[i]] = vinputs[i];
  }
  auto exp_output = circ.evaluate(inputs);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < 4; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<4>>(i, 10000, nullptr, true);
      emp::PRG prg(&seed, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(inputs);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output == exp_output);
  }
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(bool_evaluator)

BOOST_AUTO_TEST_CASE(recon_shares) {
  const size_t num = 64;

  std::mt19937 gen(200);
  std::bernoulli_distribution distrib;
  std::vector<BoolRing> secrets(num);
  for (auto& secret : secrets) {
    secret = distrib(gen);
  }

  std::vector<std::future<std::vector<BoolRing>>> parties;
  for (int i = 0; i < 4; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      io::NetIOMP<4> network(i, 10000, nullptr, true);

      // Prepare dummy shares.
      emp::PRG prg(&emp::zero_block, 200);
      std::array<std::vector<BoolRing>, 3> recon_shares;
      for (auto& secret : secrets) {
        DummyShare<BoolRing> dshare(secret, prg);
        auto share = dshare.getRSS(i);
        for (size_t i = 0; i < 3; ++i) {
          recon_shares[i].push_back(share[i]);
        }
      }

      JumpProvider jump(i);
      ThreadPool tpool(1);

      auto res =
          BoolEvaluator::reconstruct(i, recon_shares, network, jump, tpool);

      return res;
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    for (size_t i = 0; i < num; ++i) {
      BOOST_TEST(output[i] == secrets[i]);
    }
  }
}

BOOST_AUTO_TEST_SUITE_END()
*/
