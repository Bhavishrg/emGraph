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

// common::utils::Circuit<BoolRing> LTZCircuit(int repeat) {
//     common::utils::Circuit<BoolRing> circ = common::utils::Circuit<BoolRing>::generateParaPrefixAND(repeat);
//     return circ;  
// }
common::utils::Circuit<Field> LTZCircuit(int repeat) {
    std::cout<< "repeat = " << repeat << std::endl;
    common::utils::Circuit<Field> circ;
    std::vector<wire_t> w_ltz(repeat);
    std::vector<wire_t> w_ltz_o(repeat);
    for(size_t i = 0; i < repeat; i++) {
        w_ltz[i] = circ.newInputWire();
        w_ltz_o[i] = circ.addGate(GateType::kLtz, w_ltz[i]);
        circ.setAsOutput(w_ltz[i]);
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

    auto gates_per_level = opts["gates-per-level"].as<size_t>();
    auto depth = opts["depth"].as<size_t>();
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
    output_data["details"] = {{"gates_per_level", gates_per_level},
                                {"depth", depth},
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

    auto circ = LTZCircuit(2 * gates_per_level + depth ).orderGatesByLevel();
    std::cout << "--- Circuit ---\n";
    std::cout << circ << std::endl;

    std::unordered_map<common::utils::wire_t, int> input_pid_map;
    std::unordered_map<common::utils::wire_t, Field> input_map;
    // std::unordered_map<common::utils::wire_t, BoolRing> bit_mask_map;
    // std::vector<AuthAddShare<BoolRing>> output_mask;
    // std::vector<TPShare<BoolRing>>   output_tpmask;
    for (const auto& g : circ.gates_by_level[0]) {
        if (g->type == common::utils::GateType::kInp) {
        input_pid_map[g->out] = 1;
        input_map[g->out] = 1;
        // bit_mask_map[g->out] = 0;
        }
    }

    emp::PRG prg(&emp::zero_block, seed);
    

    for (size_t r = 0; r < repeat; ++r) {
        OfflineEvaluator off_eval(nP, pid, network, circ, security_param, 1, seed);
        
        
        StatsPoint start(*network);
        auto preproc = off_eval.run(input_pid_map);
        // , bit_mask_map, output_mask, output_tpmask);
        // StatsPoint end_pre(*network);
        
        // OnlineEvaluator eval(nP, pid, network, std::move(preproc), circ, seed);
        OnlineEvaluator eval(nP, pid, network, std::move(preproc), circ, security_param, 1, seed);
        
        
        eval.setRandomInputs();
        
        // network->sync();
        
        // StatsPoint start(*network);
        for (size_t i = 0; i < circ.gates_by_level.size(); ++i) {
            eval.evaluateGatesAtDepth(i);
            
        }
        // auto res = eval.evaluateCircuit(input_map);
        
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
    output_data["stats"] = {{"peak_virtual_memory", peakVirtualMemory()},
                            {"peak_resident_set_size", peakResidentSetSize()}};

    std::cout << "--- Statistics ---\n";
    for (const auto& [key, value] : output_data["stats"].items()) {
        std::cout << key << ": " << value << "\n";
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
        ("gates-per-level,g", bpo::value<size_t>()->required(), "Number of gates at each level.")
        ("depth,d", bpo::value<size_t>()->required(), "Multiplicative depth of circuit.")
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
