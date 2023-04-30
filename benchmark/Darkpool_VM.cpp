#include <io/netmp.h>
#include <dirigent/offline_evaluator.h>
#include <dirigent/online_evaluator.h>
#include <utils/circuit.h>

#include <algorithm>
#include <boost/program_options.hpp>
#include <cmath>
#include <iostream>
#include <memory>

#include "utils.h"

using namespace dirigent;
using json = nlohmann::json;
namespace bpo = boost::program_options;

quadsquad::utils::Circuit<Field> Post_LTZ_Circuit(int N, int M) {
    quadsquad::utils::Circuit<Field> circ;
    std::vector<quadsquad::utils::wire_t> inp(3);
    for(int i = 0; i < 3; i++) {
        inp[i] = circ.newInputWire();
    }
    // Mult for BitA
    quadsquad::utils::wire_t bita = circ.addGate(quadsquad::utils::GateType::kMul, inp[0], inp[1]);
    // Mult for ObSel
    quadsquad::utils::wire_t sel = circ.addGate(quadsquad::utils::GateType::kMul, bita, inp[2]);
    std::vector<quadsquad::utils::wire_t> add_gates(N + M + N*N + M*M);
    add_gates[0] = circ.addGate(quadsquad::utils::GateType::kAdd, inp[0], inp[1]);
    add_gates[1] = circ.addGate(quadsquad::utils::GateType::kAdd, inp[0], inp[2]);
    for(int i = 2 ; i < N + M + N*N + M*M - 1; i++) {
        add_gates[i] = circ.addGate(quadsquad::utils::GateType::kAdd, add_gates[i-2], add_gates[i-1]);
    }
    return circ;
}

quadsquad::utils::Circuit<Field> Post_Para_LTZ_Circuit(int N, int M) {
    quadsquad::utils::Circuit<Field> circ;
    int T = N + M;
    std::vector<quadsquad::utils::wire_t> inp(2 * T);
    for(int i = 0; i < 2 * T; i++) {
        inp[i] = circ.newInputWire();
    }
    // Mult for BitA
    std::vector<quadsquad::utils::wire_t> bita(2 * T);
    for(int i = 0; i < (2 * T) - 1; i++) {
        bita[i] = circ.addGate(quadsquad::utils::GateType::kMul, inp[i], inp[i + 1]);
    }
    bita[(2 * T) - 1] = circ.addGate(quadsquad::utils::GateType::kMul, inp[(2 * T) - 1], inp[0]);
    Field cons = 5;
    std::vector<quadsquad::utils::wire_t> add1(T);
    std::vector<quadsquad::utils::wire_t> mul1(T);
    std::vector<quadsquad::utils::wire_t> add2(T);
    std::vector<quadsquad::utils::wire_t> cons_add1(T);
    std::vector<quadsquad::utils::wire_t> mul2(T);
    for(int i = 0; i < T; i++) {
        add1[i] = circ.addGate(quadsquad::utils::GateType::kAdd, bita[2*i], inp[2 * i + 1]);
        mul1[i] = circ.addGate(quadsquad::utils::GateType::kMul, bita[i], add1[i]);
        add2[i] = circ.addGate(quadsquad::utils::GateType::kAdd, mul1[i], add1[i]); 
        cons_add1[i] = circ.addConstOpGate(quadsquad::utils::GateType::kConstAdd, add2[i], cons);
        mul2[i] = circ.addGate(quadsquad::utils::GateType::kMul, cons_add1[i], mul1[i]);
    }
    return circ;

}


void benchmark(const bpo::variables_map& opts) {
    bool save_output = false;
    std::string save_file;
    if (opts.count("output") != 0) {
        save_output = true;
        save_file = opts["output"].as<std::string>();
    }

    auto buy_list_size = opts["buy-list-size"].as<size_t>();
    auto sell_list_size = opts["sell-list-size"].as<size_t>();
    auto nP = opts["num-parties"].as<size_t>();
    auto pid = opts["pid"].as<size_t>();
    auto security_param = opts["security-param"].as<size_t>();
    auto threads = opts["threads"].as<size_t>();
    auto seed = opts["seed"].as<size_t>();
    auto repeat = opts["repeat"].as<size_t>();
    auto port = opts["port"].as<int>();

    std::shared_ptr<io::NetIOMP> network = nullptr;
    if (opts["localhost"].as<bool>()) {
        network = std::make_shared<io::NetIOMP>(pid, nP+1, port, nullptr, true);
    }
    else {
        std::ifstream fnet(opts["net-config"].as<std::string>());
        if (!fnet.good()) {
        fnet.close();
        throw std::runtime_error("Could not open network config file");
        }
        json netdata;
        fnet >> netdata;
        fnet.close();

        std::vector<std::string> ipaddress(nP+1);
        std::array<char*, 5> ip{};
        for (size_t i = 0; i < nP+1; ++i) {
            ipaddress[i] = netdata[i].get<std::string>();
            ip[i] = ipaddress[i].data();
        }

        network = std::make_shared<io::NetIOMP>(pid, nP+1, port, ip.data(), false);
    }

    json output_data;
    output_data["details"] = {{"buy_list_size", buy_list_size},
                                {"sell_list_size", sell_list_size},
                                {"num-parties", nP},
                                {"pid", pid},
                                {"security_param", security_param},
                                {"threads", threads},
                                {"seed", seed},
                                {"repeat", repeat}};
    output_data["benchmarks"] = json::array();

    std::cout << "--- Details ---\n";
    for (const auto& [key, value] : output_data["details"].items()) {
        std::cout << key << ": " << value << "\n";
    }
    std::cout << std::endl;

    size_t N = sell_list_size;
    size_t M = buy_list_size;

    auto LTZ_circ = quadsquad::utils::Circuit<BoolRing>::generatePrefixAND().orderGatesByLevel();
    auto post_LTZ_circ = Post_LTZ_Circuit(N,M).orderGatesByLevel();
    auto para_LTZ_circ = quadsquad::utils::Circuit<BoolRing>::generateParaPrefixAND(2 * (N + M)).orderGatesByLevel();
    auto post_para_LTZ_circ = Post_Para_LTZ_Circuit(N,M).orderGatesByLevel();

    std::unordered_map<quadsquad::utils::wire_t, int> LTZ_input_pid_map;
    std::unordered_map<quadsquad::utils::wire_t, BoolRing> LTZ_input_map;
    std::unordered_map<quadsquad::utils::wire_t, BoolRing> LTZ_bit_mask_map;
    std::vector<AuthAddShare<BoolRing>> LTZ_output_mask;
    std::vector<TPShare<BoolRing>> LTZ_output_tpmask;

    for (const auto& g : LTZ_circ.gates_by_level[0]) {
        if (g->type == quadsquad::utils::GateType::kInp) {
            LTZ_input_pid_map[g->out] = 1;
            LTZ_input_map[g->out] = 1;
            LTZ_bit_mask_map[g->out] = 0;
        }
    }

    std::unordered_map<quadsquad::utils::wire_t, int> post_LTZ_input_pid_map;
    std::unordered_map<quadsquad::utils::wire_t, Field> post_LTZ_input_map;

    for (const auto& g : post_LTZ_circ.gates_by_level[0]) {
        if (g->type == quadsquad::utils::GateType::kInp) {
            post_LTZ_input_pid_map[g->out] = 1;
            post_LTZ_input_map[g->out] = 1;
        }
    }

    std::unordered_map<quadsquad::utils::wire_t, int> para_LTZ_input_pid_map;
    std::unordered_map<quadsquad::utils::wire_t, BoolRing> para_LTZ_input_map;
    std::unordered_map<quadsquad::utils::wire_t, BoolRing> para_LTZ_bit_mask_map;
    std::vector<AuthAddShare<BoolRing>> para_LTZ_output_mask;
    std::vector<TPShare<BoolRing>> para_LTZ_output_tpmask;

    for (const auto& g : para_LTZ_circ.gates_by_level[0]) {
        if (g->type == quadsquad::utils::GateType::kInp) {
            para_LTZ_input_pid_map[g->out] = 1;
            para_LTZ_input_map[g->out] = 1;
            para_LTZ_bit_mask_map[g->out] = 0;
        }
    }

    std::unordered_map<quadsquad::utils::wire_t, int> post_para_LTZ_input_pid_map;
    std::unordered_map<quadsquad::utils::wire_t, Field> post_para_LTZ_input_map;

    for (const auto& g : post_para_LTZ_circ.gates_by_level[0]) {
        if (g->type == quadsquad::utils::GateType::kInp) {
            post_para_LTZ_input_pid_map[g->out] = 1;
            post_para_LTZ_input_map[g->out] = 1;
        }
    }

    emp::PRG prg(&emp::zero_block, seed);

    for (size_t r = 0; r < repeat; ++r) {
        StatsPoint start(*network);
        
        // Offline
        OfflineBoolEvaluator LTZ_off_eval(nP, pid, network, LTZ_circ, seed);
        OfflineEvaluator post_LTZ_off_eval(nP, pid, network, post_LTZ_circ, security_param, threads, seed);
        OfflineBoolEvaluator para_LTZ_off_eval(nP, pid, network, para_LTZ_circ, seed);
        OfflineEvaluator post_para_LTZ_off_eval(nP, pid, network, post_para_LTZ_circ, security_param, threads, seed);
        
        auto LTZ_preproc = LTZ_off_eval.run(LTZ_input_pid_map, LTZ_bit_mask_map, LTZ_output_mask, LTZ_output_tpmask);
        auto post_LTZ_preproc = post_LTZ_off_eval.run(post_LTZ_input_pid_map);
        auto para_LTZ_preproc = para_LTZ_off_eval.run(para_LTZ_input_pid_map, para_LTZ_bit_mask_map, para_LTZ_output_mask, para_LTZ_output_tpmask);
        auto post_para_LTZ_preproc = post_para_LTZ_off_eval.run(post_para_LTZ_input_pid_map);

        //Online
        BoolEvaluator LTZ_eval(nP, pid, network, std::move(LTZ_preproc), LTZ_circ, seed);
        OnlineEvaluator post_LTZ_eval(nP, pid, network, std::move(post_LTZ_preproc), post_LTZ_circ, security_param, threads, seed);
        BoolEvaluator para_LTZ_eval(nP, pid, network, std::move(para_LTZ_preproc), para_LTZ_circ, seed);
        OnlineEvaluator post_para_LTZ_eval(nP, pid, network, std::move(post_para_LTZ_preproc), post_para_LTZ_circ, security_param, threads, seed);

        auto LTZ_res = LTZ_eval.evaluateCircuit(LTZ_input_map);
        auto post_LTZ_res = post_LTZ_eval.evaluateCircuit(post_LTZ_input_map);
        auto para_LTZ_res = para_LTZ_eval.evaluateCircuit(para_LTZ_input_map);
        auto post_para_LTZ_res = post_para_LTZ_eval.evaluateCircuit(post_para_LTZ_input_map);


        StatsPoint end(*network);
        auto rbench = end - start;
        output_data["benchmarks"].push_back(rbench);
        size_t bytes_sent = 0;
        for (const auto& val : rbench["communication"]) {
            bytes_sent += val.get<int64_t>();
        }

        std::cout << "--- Repetition " << r + 1 << " ---\n";
        std::cout << "time: " << rbench["time"] << " ms\n";
        std::cout << "sent: " << bytes_sent << " bytes\n";

        if (save_output) {
            saveJson(output_data, save_file);
        }
        std::cout << std::endl;
    }



}

bpo::options_description programOptions() {
    bpo::options_description desc("Following options are supported by config file too.");
    desc.add_options()
        ("buy-list-size,b", bpo::value<size_t>()->required(), "Buy list size.")
        ("sell-list-size,s", bpo::value<size_t>()->required(), "Sell list size.")
        ("num-parties,n", bpo::value<size_t>()->required(), "Number of parties.")
        ("pid,p", bpo::value<size_t>()->required(), "Party ID.")
        ("security-param", bpo::value<size_t>()->default_value(128), "Security parameter in bits.")
        ("threads,t", bpo::value<size_t>()->default_value(6), "Number of threads (recommended 6).")
        ("seed", bpo::value<size_t>()->default_value(200), "Value of the random seed.")
        ("net-config", bpo::value<std::string>(), "Path to JSON file containing network details of all parties.")
        ("localhost", bpo::bool_switch(), "All parties are on same machine.")
        ("port", bpo::value<int>()->default_value(10000), "Base port for networking.")
        ("output,o", bpo::value<std::string>(), "File to save benchmarks.")
        ("repeat,r", bpo::value<size_t>()->default_value(1), "Number of times to run benchmarks.");

  return desc;
}
// clang-format on

int main(int argc, char* argv[]) {
    auto prog_opts(programOptions());

    bpo::options_description cmdline(
      "Benchmark online phase for multiplication gates.");
    cmdline.add(prog_opts);
    cmdline.add_options()(
      "config,c", bpo::value<std::string>(),
      "configuration file for easy specification of cmd line arguments")(
      "help,h", "produce help message");

    bpo::variables_map opts;
    bpo::store(bpo::command_line_parser(argc, argv).options(cmdline).run(), opts);

    if (opts.count("help") != 0) {
        std::cout << cmdline << std::endl;
        return 0;
    }

    if (opts.count("config") > 0) {
        std::string cpath(opts["config"].as<std::string>());
        std::ifstream fin(cpath.c_str());

        if (fin.fail()) {
            std::cerr << "Could not open configuration file at " << cpath << "\n";
            return 1;
        }

        bpo::store(bpo::parse_config_file(fin, prog_opts), opts);
    }

    try {
        bpo::notify(opts);

        // Check if output file already exists.
        if (opts.count("output") != 0) {
            std::ifstream ftemp(opts["output"].as<std::string>());
            if (ftemp.good()) {
                ftemp.close();
                throw std::runtime_error("Output file aready exists.");
            }
            ftemp.close();
        }

        if (!opts["localhost"].as<bool>() && (opts.count("net-config") == 0)) {
            throw std::runtime_error("Expected one of 'localhost' or 'net-config'");
        }
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    try {
        benchmark(opts);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\nFatal error" << std::endl;
        return 1;
    }

    return 0;
}