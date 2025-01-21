#include <io/netmp.h>
#include <asterisk/offline_evaluator.h>
#include <asterisk/online_evaluator.h>
#include <utils/circuit.h>

#include <algorithm>
#include <boost/program_options.hpp>
#include <cmath>
#include <iostream>
#include <memory>
#include <omp.h>

#include "utils.h"

using namespace asterisk;
using json = nlohmann::json;
namespace bpo = boost::program_options;

common::utils::Circuit<Ring> generateCircuit(std::shared_ptr<io::NetIOMP> &network, int nP, int pid, size_t vec_size) {

    std::cout << "Generating circuit" << std::endl;

    common::utils::Circuit<Ring> circ;
    int in_size = vec_size / nP;
    for (int p = 0; p < nP; ++p) {
        std::vector<wire_t> input(in_size);
        for (int i = 0; i < input.size(); ++i) {
            input[i] = circ.newInputWire();
            // std::cout << "in " << input[i] << std::endl;
        }
        auto tmp = circ.addMGate(common::utils::GateType::kPermAndSh, input, p + 1);
        auto out = circ.addMGate(common::utils::GateType::kPermAndSh, tmp, p + 1);
        for (int i = 0; i < out.size(); ++i) {
            // std::cout << "out " << out[i] << std::endl;
            circ.setAsOutput(out[i]);
        }
    }

    // auto mul = circ.addGate(common::utils::GateType::kMul, input[0], input[1]);
    
    // std::vector<wire_t> input(vec_size);
    // for (int i = 0; i < vec_size; ++i) {
    //     input[i] = circ.newInputWire();
    // }
    // auto out = circ.addMOGate(common::utils::GateType::kAmortzdPnS, input, nP);
    // for (int i = 0; i < out.size(); ++i) {
    //     for (int j = 0; j < out[i].size(); ++j) {
    //         circ.setAsOutput(out[i][j]);
    //     }
    // }

    // for (int i = 0; i < nP; ++i) {
        // auto input1 = circ.newInputWire();
        // auto input2 = circ.newInputWire();
        // auto input3 = circ.newInputWire();
        // auto input4 = circ.newInputWire();
        // auto cmp = circ.addGate(common::utils::GateType::kMul3, input1, input2, input4);
        // circ.setAsOutput(cmp);
    // }

    // auto input = circ.newInputWire();
    // auto cmp = circ.addGate(common::utils::GateType::kLtz, input);
    // circ.setAsOutput(cmp);

    // for (int j = 0; j < nP; ++j) {
        // std::vector<wire_t> in1(10);
        // std::vector<wire_t> in2(10);
        // for (int i = 0; i < in1.size(); ++i) {
        //     in1[i] = circ.newInputWire();
        //     in2[i] = circ.newInputWire();
        // }
        // auto out = circ.addGate(common::utils::GateType::kDotprod, in1, in2);
        // auto out2 = circ.addGate(common::utils::GateType::kMul, out, in1[0]);
        // circ.setAsOutput(out);
    // }

    return circ;
}


void benchmark(const bpo::variables_map& opts) {

    bool save_output = false;
    std::string save_file;
    if (opts.count("output") != 0) {
        save_output = true;
        save_file = opts["output"].as<std::string>();
    }

    auto nP = opts["num-parties"].as<int>();
    auto vec_size = opts["vec-size"].as<size_t>();
    auto pid = opts["pid"].as<size_t>();
    auto threads = opts["threads"].as<size_t>();
    auto seed = opts["seed"].as<size_t>();
    auto repeat = opts["repeat"].as<size_t>();
    auto port = opts["port"].as<int>();
    omp_set_num_threads(nP);
    std::cout << "Starting benchmarks " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;

    std::shared_ptr<io::NetIOMP> network = nullptr;
    if (opts["localhost"].as<bool>()) {
        network = std::make_shared<io::NetIOMP>(pid, nP + 1, port, nullptr, true);
    } else {
        std::ifstream fnet(opts["net-config"].as<std::string>());
        if (!fnet.good()) {
            fnet.close();
            throw std::runtime_error("Could not open network config file");
        }
        json netdata;
        fnet >> netdata;
        fnet.close();
        std::vector<std::string> ipaddress(nP + 1);
        std::array<char*, 5> ip{};
        for (size_t i = 0; i < nP + 1; ++i) {
            ipaddress[i] = netdata[i].get<std::string>();
            ip[i] = ipaddress[i].data();
        }
        network = std::make_shared<io::NetIOMP>(pid, nP + 1, port, ip.data(), false);
    }
    std::cout << "Network set " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;

    json output_data;
    output_data["details"] = {{"num_parties", nP},
                              {"vec_size", vec_size},
                              {"pid", pid},
                              {"threads", threads},
                              {"seed", seed},
                              {"repeat", repeat}};
    output_data["benchmarks"] = json::array();

    std::cout << "--- Details ---" << std::endl;
    for (const auto& [key, value] : output_data["details"].items()) {
        std::cout << key << ": " << value << std::endl;
    }
    std::cout << std::endl;

    auto circ = generateCircuit(network, nP, pid, vec_size).orderGatesByLevel();

    std::cout << "--- Circuit --- " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
    std::cout << circ << std::endl;
    
    std::unordered_map<common::utils::wire_t, int> input_pid_map;

    for (const auto& g : circ.gates_by_level[0]) {
        if (g->type == common::utils::GateType::kInp) {
            input_pid_map[g->out] = 1;
        }
    }

    network->sync();
    StatsPoint start(*network);
    emp::PRG prg(&emp::zero_block, seed);
    OfflineEvaluator off_eval(nP, pid, network, circ, threads, seed);

    auto preproc = off_eval.run(input_pid_map, vec_size);
    std::cout << "Preprocessing complete " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
    network->sync();
    std::cout << "Starting Online Evaluation " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;

    StatsPoint online_start(*network);
    OnlineEvaluator eval(nP, pid, network, std::move(preproc), circ, threads, seed);

    eval.setRandomInputs();
    std::cout << "Inputs set" << std::endl;

    for (size_t i = 0; i < circ.gates_by_level.size(); ++i) {
        eval.evaluateGatesAtDepth(i);
    }
    std::cout << "Online Eval complete" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
    StatsPoint end_test(*network);
    network->sync();
    StatsPoint end(*network);

    auto preproc_rbench = online_start - start;
    auto online_rbench = end - online_start;
    auto online_test_rbench = end_test - online_start;
    auto rbench = end - start;
    output_data["benchmarks"].push_back(preproc_rbench);
    output_data["benchmarks"].push_back(online_rbench);
    output_data["benchmarks"].push_back(online_test_rbench);
    output_data["benchmarks"].push_back(rbench);

    size_t pre_bytes_sent = 0;
    for (const auto& val : preproc_rbench["communication"]) {
        pre_bytes_sent += val.get<int64_t>();
    }
    size_t online_bytes_sent = 0;
    for (const auto& val : online_rbench["communication"]) {
        online_bytes_sent += val.get<int64_t>();
    }
    size_t bytes_sent = 0;
    for (const auto& val : rbench["communication"]) {
        bytes_sent += val.get<int64_t>();
    }

    // std::cout << "--- Repetition " << r + 1 << " ---" << std::endl;
    std::cout << "preproc time: " << preproc_rbench["time"] << " ms" << std::endl;
    std::cout << "preproc sent: " << pre_bytes_sent << " bytes" << std::endl;
    std::cout << "online test time: " << online_test_rbench["time"] << " ms" << std::endl;
    std::cout << "online time: " << online_rbench["time"] << " ms" << std::endl;
    std::cout << "online sent: " << online_bytes_sent << " bytes" << std::endl;
    std::cout << "total time: " << rbench["time"] << " ms" << std::endl;
    std::cout << "total sent: " << bytes_sent << " bytes" << std::endl;
    std::cout << std::endl;

    output_data["stats"] = {{"peak_virtual_memory", peakVirtualMemory()},
                            {"peak_resident_set_size", peakResidentSetSize()}};

    std::cout << "--- Statistics ---" << std::endl;
    for (const auto& [key, value] : output_data["stats"].items()) {
        std::cout << key << ": " << value << std::endl;
    }
    std::cout << std::endl;

    if (save_output) {
        saveJson(output_data, save_file);
    }
}

// clang-format off
bpo::options_description programOptions() {
    bpo::options_description desc("Following options are supported by config file too.");
    desc.add_options()
        ("num-parties,n", bpo::value<int>()->required(), "Number of parties.")
        ("vec-size,v", bpo::value<size_t>()->required(), "Number of gates at each level.")
        ("pid,p", bpo::value<size_t>()->required(), "Party ID.")
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
    bpo::options_description cmdline("Benchmark online phase for multiplication gates.");
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
            std::cerr << "Could not open configuration file at " << cpath << std::endl;
            return 1;
        }
        bpo::store(bpo::parse_config_file(fin, prog_opts), opts);
    }
    try {
        bpo::notify(opts);
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