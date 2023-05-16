#define BOOST_TEST_MODULE offline

#include <emp-tool/emp-tool.h>
#include <io/netmp.h>
#include <utils/helpers.h>
#include <assistedMPC/offline_evaluator.h>
#include <assistedMPC/rand_gen_pool.h>
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

using namespace assistedMPC;
namespace bdata = boost::unit_test::data;
 

constexpr int TEST_DATA_MAX_VAL = 1000;
constexpr int SECURITY_PARAM = 128;

struct GlobalFixture {
  GlobalFixture() {
    NTL::ZZ_p::init(NTL::conv<NTL::ZZ>("18446744073709551616"));
  }
};

BOOST_GLOBAL_FIXTURE(GlobalFixture);

BOOST_AUTO_TEST_SUITE(offline_evaluator)

BOOST_AUTO_TEST_CASE(random_share) {
  int nP = 5;
  
  std::vector<std::future<AuthAddShare<Field>>> parties;
  TPShare<Field> tpshares;
  for (int i = 0; i <= nP; i++) {
    parties.push_back(std::async(std::launch::async, [&, i]() { 
      AuthAddShare<Field> shares;
      size_t idx = 0;
      RandGenPool vrgen(i, nP);
      std::vector<Field> keySh(nP + 1);
      Field key = 0;
      if(i == 0)  {
        key = 0;
        keySh[0] = 0;
        for(int j = 1; j <= nP; j++) {
            vrgen.pi(j).random_data(&keySh[j], sizeof(Field));
            key += keySh[j];
        }
      }
      else {
        vrgen.p0().random_data(&key, sizeof(Field));
      }
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      if(i == 0) { 
        std::vector<std::vector<Field>> rand_sh(nP + 1);
        OfflineEvaluator::randomShare_Helper(nP, vrgen, shares, tpshares, key, keySh, rand_sh);
        size_t rand_sh_num = rand_sh[1].size();
        for(size_t j = 1; j <= nP; j++) {
            network->send(j, &rand_sh_num, sizeof(size_t));
            network->send(j, rand_sh[j-1].data(), sizeof(Field) * rand_sh_num);
        }
      }
      else {
        size_t rand_sh_num;
        network->recv(0, &rand_sh_num, sizeof(size_t));
        std::vector<Field> rand_sh(rand_sh_num);
        network->recv(0, rand_sh.data(), sizeof(Field) * rand_sh_num);
        OfflineEvaluator::randomShare_Party(shares, key, rand_sh, idx);
      }

      return shares;
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

  BOOST_AUTO_TEST_CASE(random_share_secret) {
  int nP = 5;
  
  std::vector<std::future<AuthAddShare<Field>>> parties;
  TPShare<Field> tpshares;
  for (int i = 0; i <= nP; i++) {
    parties.push_back(std::async(std::launch::async, [&, i]() { 
      AuthAddShare<Field> shares;
      size_t idx = 0;
      RandGenPool vrgen(i, nP);
      std::vector<Field> keySh(nP + 1);
      Field key = 0;
      if(i == 0)  {
        key = 0;
        keySh[0] = 0;
        for(int j = 1; j <= nP; j++) {
            vrgen.pi(j).random_data(&keySh[j], sizeof(Field));
            key += keySh[j];
        }
      }
      else {
        vrgen.p0().random_data(&key, sizeof(Field));
      }
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      if(i == 0) { 
        Field secret;
        vrgen.self().random_data(&secret, sizeof(Field));
        std::vector<std::vector<Field>> rand_sh_sec(nP + 1);
        OfflineEvaluator::randomShareSecret_Helper(nP, vrgen, shares, tpshares, secret, key, keySh, rand_sh_sec);
        size_t rand_sh_sec_num = rand_sh_sec[1].size();
        for(size_t j = 1; j <= nP; j++) {
            network->send(j, &rand_sh_sec_num, sizeof(size_t));
            network->send(j, rand_sh_sec[j-1].data(), sizeof(Field) * rand_sh_sec_num);
        }
      }
      else {
        size_t rand_sh_sec_num;
        network->recv(0, &rand_sh_sec_num, sizeof(size_t));
        std::vector<Field> rand_sh_sec(rand_sh_sec_num);
        network->recv(0, rand_sh_sec.data(), sizeof(Field) * rand_sh_sec_num);
        OfflineEvaluator::randomShare_Party(shares, key, rand_sh_sec, idx);
      }

      return shares;
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

  BOOST_AUTO_TEST_CASE(random_share_party) {
  int nP = 5;
  int dealer = 1;
  std::vector<std::future<AuthAddShare<Field>>> parties;
  TPShare<Field> tpshares;
  for (int i = 0; i <= nP; i++) {
    parties.push_back(std::async(std::launch::async, [&, i]() { 
      AuthAddShare<Field> shares;
      size_t idx = 0;
      RandGenPool vrgen(i, nP);
      std::vector<Field> keySh(nP + 1);
      Field key = 0;
      if(i == 0)  {
        key = 0;
        keySh[0] = 0;
        for(int j = 1; j <= nP; j++) {
            vrgen.pi(j).random_data(&keySh[j], sizeof(Field));
            key += keySh[j];
        }
      }
      else {
        vrgen.p0().random_data(&key, sizeof(Field));
      }
      auto network = std::make_shared<io::NetIOMP>(i, nP+1, 10000, nullptr, true);
      if(i == 0) { 
        Field secret;
        vrgen.self().random_data(&secret, sizeof(Field));
        std::vector<std::vector<Field>> rand_sh_sec(nP + 1);
        OfflineEvaluator::randomShareSecret_Helper(nP, vrgen, shares, tpshares, secret, key, keySh, rand_sh_sec);
        size_t rand_sh_sec_num = rand_sh_sec[1].size();
        for(size_t j = 1; j <= nP; j++) {
            network->send(j, &rand_sh_sec_num, sizeof(size_t));
            network->send(j, rand_sh_sec[j-1].data(), sizeof(Field) * rand_sh_sec_num);
        }
      }
      else {
        size_t rand_sh_sec_num;
        network->recv(0, &rand_sh_sec_num, sizeof(size_t));
        std::vector<Field> rand_sh_sec(rand_sh_sec_num);
        network->recv(0, rand_sh_sec.data(), sizeof(Field) * rand_sh_sec_num);
        OfflineEvaluator::randomShare_Party(shares, key, rand_sh_sec, idx);
      }

      return shares;
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

  BOOST_AUTO_TEST_SUITE_END()