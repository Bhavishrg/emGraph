#define BOOST_TEST_MODULE offline

#include <emp-tool/emp-tool.h>
#include <io/netmp.h>
#include <dirigent/helpers.h>
#include <dirigent/offline_evaluator.h>
#include <dirigent/rand_gen_pool.h>
#include <utils/circuit.h>

#include <algorithm>
#include <boost/algorithm/hex.hpp>
#include <boost/test/data/monomorphic.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/included/unit_test.hpp>
#include <future>
#include <memory>
#include <random>
#include <vector>

using namespace dirigent;
namespace bdata = boost::unit_test::data;
 

constexpr int TEST_DATA_MAX_VAL = 1000;
constexpr int SECURITY_PARAM = 128;

struct GlobalFixture {
  GlobalFixture() {
    NTL::ZZ_p::init(NTL::conv<NTL::ZZ>("18446744073709551616"));

    //NTL::ZZ_pX P(NTL::INIT_MONO, 47);
    //NTL::SetCoeff(P, 5);
    //NTL::SetCoeff(P, 0);
    //NTL::ZZ_pE::init(P);
  }
};
/*
BOOST_GLOBAL_FIXTURE(GlobalFixture);

BOOST_AUTO_TEST_SUITE(dummy_offline)

BOOST_AUTO_TEST_CASE(depth_2_circuit) {
  utils::Circuit<Ring> circ;
  std::vector<utils::wire_t> input_wires;
  std::unordered_map<utils::wire_t, int> input_pid_map;
  for (size_t i = 0; i < 4; ++i) {
    auto winp = circ.newInputWire();
    input_wires.push_back(winp);
    input_pid_map[winp] = 0;
  }
  auto w_aab =
      circ.addGate(utils::GateType::kAdd, input_wires[0], input_wires[1]);
  auto w_cmd =
      circ.addGate(utils::GateType::kMul, input_wires[2], input_wires[3]);
  auto w_mout = circ.addGate(utils::GateType::kMul, w_aab, w_cmd);
  auto w_aout = circ.addGate(utils::GateType::kAdd, w_aab, w_cmd);
  circ.setAsOutput(w_mout);
  circ.setAsOutput(w_aout);

  auto level_circ = circ.orderGatesByLevel();
  std::vector<PreprocCircuit<Ring>> v_preproc;
  auto seed = emp::makeBlock(100, 200);
  for (int i = 0; i < 4; ++i) {
    emp::PRG prg(&seed, 0);
    v_preproc.push_back(
        OfflineEvaluator::dummy(level_circ, input_pid_map, 128, i, prg));
  }

  for (int i = 0; i < 4; ++i) {
    BOOST_TEST(v_preproc[i].gates.size() == level_circ.num_gates);
    for (int j = i + 1; j < 4; ++j) {
      const auto& preproc_i = v_preproc[i];
      const auto& preproc_j = v_preproc[j];

      for (size_t k = 0; k < preproc_i.gates.size(); ++k) {
        auto mask_i = preproc_i.gates[k]->mask.commonValueWithParty(i, j);
        auto mask_j = preproc_j.gates[k]->mask.commonValueWithParty(j, i);
        BOOST_TEST(mask_i == mask_j);

        auto* gi =
            dynamic_cast<PreprocMultGate<Ring>*>(preproc_i.gates[k].get());
        if (gi != nullptr) {
          auto* gj =
              dynamic_cast<PreprocMultGate<Ring>*>(preproc_j.gates[k].get());
          auto prod_i = gi->mask_prod.commonValueWithParty(i, j);
          auto prod_j = gj->mask_prod.commonValueWithParty(j, i);
          BOOST_TEST(prod_i == prod_j);
        }
      }

      for (size_t k = 0; k < preproc_i.output.size(); ++k) {
        const auto& opening_i =
            preproc_i.output[k].openings[offsetFromPid(i, j) - 1];
        const auto& opening_j =
            preproc_j.output[k].openings[offsetFromPid(j, i) - 1];
        BOOST_TEST(opening_i == opening_j);

        const auto& commitment_i =
            preproc_i.output[k].commitments[3 - offsetFromPid(i, j)];
        const auto& commitment_j =
            preproc_j.output[k].commitments[3 - offsetFromPid(j, i)];
        BOOST_TEST(commitment_i == commitment_j);
      }
    }
  }
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(ot_provider)

BOOST_AUTO_TEST_CASE(multiply) {
  std::vector<Ring> input_a = {5};
  std::vector<Ring> input_b = {7};

  auto party_f = std::async(std::launch::async, [&]() {
    io::NetIOMP<2> network(1, 10000, nullptr, true);
    OTProvider ot(1, 0, network.get(0, false), network.get(0, true));
    emp::PRG prg(&emp::zero_block, 200);
    return ot.multiplySend(input_a, prg);
  });

  io::NetIOMP<2> network(0, 10000, nullptr, true);
  OTProvider ot(0, 1, network.get(1, true), network.get(1, false));
  auto share0 = ot.multiplyReceive(input_b);

  auto share1 = party_f.get();

  auto exp_output = input_a[0] * input_b[0];
  auto output = share0[0] + share1[0];
  BOOST_TEST(output == exp_output);
}

BOOST_DATA_TEST_CASE(multiply_list, bdata::random(0, 200) ^ bdata::xrange(1),
                     seed, idx) {
  const size_t num = 10;

  std::mt19937 engine(seed);
  std::uniform_int_distribution<Ring> distrib(0, TEST_DATA_MAX_VAL);
  std::vector<Ring> input_a(num);
  std::vector<Ring> input_b(num);
  for (size_t i = 0; i < num; ++i) {
    input_a[i] = distrib(engine);
    input_b[i] = distrib(engine);
  }

  auto party_f = std::async(std::launch::async, [&]() {
    io::NetIOMP<2> network(1, 10000, nullptr, true);
    OTProvider ot(1, 0, network.get(0, false), network.get(0, true));
    emp::PRG prg(&emp::zero_block, 200);
    return ot.multiplySend(input_a, prg);
  });

  io::NetIOMP<2> network(0, 10000, nullptr, true);
  OTProvider ot(0, 1, network.get(1, true), network.get(1, false));
  auto shares0 = ot.multiplyReceive(input_b);

  auto shares1 = party_f.get();

  for (size_t i = 0; i < num; ++i) {
    auto exp_output = input_a[i] * input_b[i];
    auto output = shares0[i] + shares1[i];
    BOOST_TEST(output == exp_output);
  }
}

BOOST_DATA_TEST_CASE(multiply_parallel,
                     bdata::random(0, 200) ^ bdata::xrange(1), seed, idx) {
  const size_t num = 10;

  std::mt19937 engine(seed);
  std::uniform_int_distribution<Ring> distrib(0, TEST_DATA_MAX_VAL);
  std::array<std::vector<Ring>, 2> inputs;
  for (size_t i = 0; i < num; ++i) {
    inputs[0].push_back(distrib(engine));
    inputs[1].push_back(distrib(engine));
  }

  auto party1_f = std::async(std::launch::async, [&]() {
    io::NetIOMP<2> network(1, 10000, nullptr, true);
    ThreadPool pool(2);
    emp::PRG prg(&emp::zero_block, 201);
    OTProvider ot(1, 0, network.get(0, false), network.get(0, true));

    auto res1_f =
        pool.enqueue([&]() { return ot.multiplySend(inputs[1], prg); });

    auto res2_f = pool.enqueue([&]() { return ot.multiplyReceive(inputs[1]); });

    auto vres1 = res1_f.get();
    auto vres2 = res2_f.get();
    vres2.insert(vres2.end(), vres1.begin(), vres1.end());

    return vres2;
  });

  io::NetIOMP<2> network(0, 10000, nullptr, true);
  ThreadPool pool(2);
  emp::PRG prg(&emp::zero_block, 200);
  OTProvider ot(0, 1, network.get(1, true), network.get(1, false));

  auto res1_f = pool.enqueue([&]() { return ot.multiplySend(inputs[0], prg); });
  auto res2_f = pool.enqueue([&]() { return ot.multiplyReceive(inputs[0]); });
  auto vres1 = res1_f.get();
  auto vres2 = res2_f.get();
  vres1.insert(vres1.end(), vres2.begin(), vres2.end());
  auto& shares0 = vres1;

  auto shares1 = party1_f.get();

  for (size_t i = 0; i < num; ++i) {
    auto exp_output = inputs[0][i] * inputs[1][i];
    auto output1 = shares0[i] + shares1[i];
    auto output2 = shares0[i + num] + shares1[i + num];
    BOOST_TEST(output1 == exp_output);
    BOOST_TEST(output2 == exp_output);
  }
}

BOOST_AUTO_TEST_SUITE_END()
*/
BOOST_AUTO_TEST_SUITE(offline_evaluator)

BOOST_AUTO_TEST_CASE(random_share) {
  int nP = 5;
  std::vector<AuthAddShare<Field>> shares(nP+1);
  TPShare<Field> tpshares;
  
  std::vector<std::future<AuthAddShare<Field>>> parties;
  
  tpshares.setKey(5);
  for (int i = 0; i <= nP; i++) {
    
    
    parties.push_back(std::async(std::launch::async, [&, i]() { 
      RandGenPool vrgen(i, nP);
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      OfflineEvaluator::randomShare(nP, i, vrgen, *network, shares[i], tpshares);

            return shares[i];
    }));
    
  }
  int i = 0;
  for (auto& p : parties) { 
    auto res = p.get();
      
        BOOST_TEST(res.valueAt() == tpshares.commonValueWithParty(i));
        BOOST_TEST(res.tagAt() == tpshares.commonTagWithParty(i));
     
      i++;
    }
  }
  //BOOST_AUTO_TEST_SUITE_END()

  
  BOOST_AUTO_TEST_CASE(random_share_with_party) {
    int nP = 10;
    
    //std::vector<AuthAddShare<Field>> shares(nP+1);
    TPShare<Field> tpshares;
  
    std::vector<std::future<AuthAddShare<Field>>> parties;
    
    tpshares.setKey(5);
    int i = 0;
    parties.push_back(std::async(std::launch::async, [&, i]() { 
        
        AuthAddShare<Field> share;
        Field secret;
        RandGenPool vrgen(i, nP);
        auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
        OfflineEvaluator::randomShareWithParty(nP, i, nP, vrgen, *network, 
                                               share, tpshares, secret);
        return share;
      }));

    for ( i = 1; i <= nP; i++) {
      
      parties.push_back(std::async(std::launch::async, [&, i]() { 
        AuthAddShare<Field> share;
        TPShare<Field> tmp;
        Field secret;
        RandGenPool vrgen(i, nP);
        auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
        OfflineEvaluator::randomShareWithParty(nP, i, nP, vrgen, *network, 
                                               share, tmp, secret);
        return share;
      }));
    
    }

    //int i = 0;
    i = 0;
    for (auto& p : parties) { 
      auto res = p.get();
      
   
        BOOST_TEST(res.valueAt() == tpshares.commonValueWithParty(i));
        BOOST_TEST(res.tagAt() == tpshares.commonTagWithParty(i));
      
      i++;
    }
    
  }

  BOOST_AUTO_TEST_CASE(random_share_secret) {
    int nP = 10;
    TPShare<Field> tpshares;
  
    std::vector<std::future<AuthAddShare<Field>>> parties;
    
    tpshares.setKey(5);
    Field secret = 134534;
    int i = 0;
    parties.push_back(std::async(std::launch::async, [&, i]() { 
        
        AuthAddShare<Field> share;
        
        RandGenPool vrgen(i, nP);
        auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
        OfflineEvaluator::randomShareSecret(nP, i, vrgen, *network, 
                                               share, tpshares, secret);
        return share;
      }));

    for ( i = 1; i <= nP; i++) {
      
      parties.push_back(std::async(std::launch::async, [&, i]() { 
        AuthAddShare<Field> share;
        TPShare<Field> tmp;
        Field secret;
        RandGenPool vrgen(i, nP);
        auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
        OfflineEvaluator::randomShareSecret(nP, i, vrgen, *network, 
                                               share, tmp, secret);
        return share;
      }));
    
    }

    //int i = 0;
    i = 0;
    for (auto& p : parties) { 
      auto res = p.get();
      
   
        BOOST_TEST(res.valueAt() == tpshares.commonValueWithParty(i));
        BOOST_TEST(res.tagAt() == tpshares.commonTagWithParty(i));
      
      i++;
    }
  }

BOOST_AUTO_TEST_CASE(depth_2_circuit) {
  int nP = 5;
  quadsquad::utils::Circuit<Field> circ;
  std::vector<quadsquad::utils::wire_t> input_wires;
  std::unordered_map<quadsquad::utils::wire_t, int> input_pid_map;
  
  for (size_t i = 0; i < 4; ++i) {
    auto winp = circ.newInputWire();
    input_wires.push_back(winp);
    input_pid_map[winp] = 1;
  }



  auto w_aab =
      circ.addGate(quadsquad::utils::GateType::kAdd, input_wires[0], input_wires[1]);
  auto w_cmd =
      circ.addGate(quadsquad::utils::GateType::kMul, input_wires[2], input_wires[3]);
  auto w_mout = circ.addGate(quadsquad::utils::GateType::kMul, w_aab, w_cmd);
  auto w_aout = circ.addGate(quadsquad::utils::GateType::kAdd, w_aab, w_cmd);
  circ.setAsOutput(w_mout);
  circ.setAsOutput(w_aout);
  auto level_circ = circ.orderGatesByLevel();

  std::vector<std::future<PreprocCircuit<Field>>> parties;
  parties.reserve(nP+1);
  
  for (int i = 0; i <= nP; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i, input_pid_map]() {
      std::cout<< "Network created"<<std::endl;
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      //std::cout<< "Network created"<<std::endl;
      OfflineEvaluator eval(nP, i, std::move(network), 
                            level_circ, SECURITY_PARAM, 4);
      std::cout<<"preprocessing completed"<<std::endl;
      return eval.run(input_pid_map);
    }));
  }

  std::vector<PreprocCircuit<Field>> v_preproc;
  v_preproc.reserve(parties.size());
  for (auto& f : parties) {
    v_preproc.push_back(f.get());
  }

  BOOST_TEST(v_preproc[0].gates.size() == level_circ.num_gates);
  const auto& preproc_0 = v_preproc[0];
  
  for (int i = 1; i <= nP; ++i) {
    BOOST_TEST(v_preproc[i].gates.size() == level_circ.num_gates);
    const auto& preproc_i = v_preproc[i];
    for(int j = 0; j < 4; j++) {
      auto tpmask = preproc_0.gates[j]->tpmask;
      auto mask_i = preproc_i.gates[j]->mask;
      BOOST_TEST(mask_i.valueAt() == tpmask.commonValueWithParty(i));
      BOOST_TEST(mask_i.tagAt() == tpmask.commonTagWithParty(i));
    }
  }
}


BOOST_AUTO_TEST_CASE(dot_product) {
  auto seed = emp::makeBlock(100, 200);
  int nf = 10;
  int nP = 5;
  quadsquad::utils::Circuit<Field> circ;
  std::vector<quadsquad::utils::wire_t> vwa(nf);
  std::vector<quadsquad::utils::wire_t> vwb(nf);
  for (int i = 0; i < nf; i++) {
    vwa[i] = circ.newInputWire();
    vwb[i] = circ.newInputWire();
  }
  auto wdotp = circ.addGate(quadsquad::utils::GateType::kDotprod, vwa, vwb);
  circ.setAsOutput(wdotp);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<quadsquad::utils::wire_t, Field> input_map;
  std::unordered_map<quadsquad::utils::wire_t, int> input_pid_map;
  std::mt19937 gen(200);
  std::uniform_int_distribution<Field> distrib(0, TEST_DATA_MAX_VAL);
  for (size_t i = 0; i < nf; ++i) {
    input_map[vwa[i]] = distrib(gen);
    input_map[vwb[i]] = distrib(gen);
    input_pid_map[vwa[i]] = 0;
    input_pid_map[vwb[i]] = 1;
  }

  auto exp_output = circ.evaluate(input_map);

  std::vector<std::future<PreprocCircuit<Field>>> parties;
  parties.reserve(nP+1);
  
  for (int i = 0; i <= nP; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i, input_pid_map]() {
      
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      
      OfflineEvaluator eval(nP, i, std::move(network), 
                            level_circ, SECURITY_PARAM, 4);
      return eval.run(input_pid_map);
    }));
  }

  std::vector<PreprocCircuit<Field>> v_preproc;
  v_preproc.reserve(parties.size());
  for (auto& f : parties) {
    v_preproc.push_back(f.get());
  }

  BOOST_TEST(v_preproc[0].gates.size() == level_circ.num_gates);
  const auto& preproc_0 = v_preproc[0];
  
  for (int i = 1; i <= nP; ++i) {
    BOOST_TEST(v_preproc[i].gates.size() == level_circ.num_gates);
    const auto& preproc_i = v_preproc[i];
    for(int j = 0; j < 4; j++) {
      auto tpmask = preproc_0.gates[j]->tpmask;
      auto mask_i = preproc_i.gates[j]->mask;
      BOOST_TEST(mask_i.valueAt() == tpmask.commonValueWithParty(i));
      BOOST_TEST(mask_i.tagAt() == tpmask.commonTagWithParty(i));
    }
  }
}


BOOST_AUTO_TEST_SUITE_END()
//=================















/*
  for (int i = 0; i < 4; ++i) {
    BOOST_TEST(v_preproc[i].gates.size() == level_circ.num_gates);
    for (int j = i + 1; j < 4; ++j) {
      const auto& preproc_i = v_preproc[i];
      const auto& preproc_j = v_preproc[j];

      for (size_t k = 0; k < preproc_i.gates.size(); ++k) {
        auto mask_i = preproc_i.gates[k]->mask.commonValueWithParty(i, j);
        auto mask_j = preproc_j.gates[k]->mask.commonValueWithParty(j, i);
        BOOST_TEST(mask_i == mask_j);

        auto* gi =
            dynamic_cast<PreprocMultGate<Ring>*>(preproc_i.gates[k].get());
        if (gi != nullptr) {
          auto* gj =
              dynamic_cast<PreprocMultGate<Ring>*>(preproc_j.gates[k].get());
          auto prod_i = gi->mask_prod.commonValueWithParty(i, j);
          auto prod_j = gj->mask_prod.commonValueWithParty(j, i);
          BOOST_TEST(prod_i == prod_j);
        }
      }

      for (size_t k = 0; k < preproc_i.output.size(); ++k) {
        const auto& opening_i =
            preproc_i.output[k].openings[offsetFromPid(i, j) - 1];
        const auto& opening_j =
            preproc_j.output[k].openings[offsetFromPid(j, i) - 1];
        BOOST_TEST(opening_i == opening_j);

        const auto& commitment_i =
            preproc_i.output[k].commitments[3 - offsetFromPid(i, j)];
        const auto& commitment_j =
            preproc_j.output[k].commitments[3 - offsetFromPid(j, i)];
        BOOST_TEST(commitment_i == commitment_j);
      }
    }
  }*/


/*BOOST_AUTO_TEST_SUITE(ring_ext_helpers)

BOOST_AUTO_TEST_CASE(io) {
  NTL::ZZ_pContext ZZ_p_ctx;
  ZZ_p_ctx.save();
  NTL::ZZ_pEContext ZZ_pE_ctx;
  ZZ_pE_ctx.save();

  auto party = std::async(std::launch::async, [=]() {
    ZZ_p_ctx.restore();
    ZZ_pE_ctx.restore();
    NTL::ZZ_pE val;

    ::io::NetIOMP<2> net(1, 10000, nullptr, true);
    receiveZZpE(net.getRecvChannel(0), &val, 1);
    sendZZpE(net.getSendChannel(0), &val, 1);
  });

  NTL::ZZ_pE val;
  NTL::random(val);
  ::io::NetIOMP<2> net(0, 10000, nullptr, true);
  sendZZpE(net.getSendChannel(1), &val, 1);

  NTL::ZZ_pE res;
  receiveZZpE(net.getRecvChannel(1), &res, 1);

  party.wait();

  BOOST_TEST(res == val);
}

BOOST_AUTO_TEST_CASE(randomize) {
  emp::PRG prg1(&emp::zero_block, 200);
  emp::PRG prg2(&emp::zero_block, 200);

  NTL::ZZ_pE val1;
  NTL::ZZ_pE val2;

  randomizeZZpE(prg1, val1);
  randomizeZZpE(prg2, val2);
  BOOST_TEST(val1 == val2);

  Ring rval = 10;
  randomizeZZpE(prg1, val1, rval);
  randomizeZZpE(prg2, val2, rval);
  BOOST_TEST(val1 == val2);
  BOOST_TEST(coeff(NTL::rep(val1), 0) == rval);
}

BOOST_AUTO_TEST_SUITE_END()
*/