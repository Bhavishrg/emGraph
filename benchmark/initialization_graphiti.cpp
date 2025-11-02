#include <io/netmp.h>
#include <grasp/offline_evaluator.h>
#include <grasp/online_evaluator.h>
#include <utils/circuit.h>

#include <algorithm>
#include <boost/program_options.hpp>
#include <cmath>
#include <iostream>
#include <memory>
#include <omp.h>

#include "utils.h"

#include <iomanip>

using namespace grasp;
using json = nlohmann::json;
namespace bpo = boost::program_options;

common::utils::Circuit<Ring> generateInitCircuit(std::shared_ptr<io::NetIOMP> &network, int nP, int pid, size_t vec_size) {


    std::cout << "Generating Initialization circuit" << std::endl;
    
    common::utils::Circuit<Ring> circ;

    // Create input wires for the vector
    std::vector<common::utils::wire_t> input_wires(vec_size);
    std::generate(input_wires.begin(), input_wires.end(), [&]() { return circ.newInputWire(); });
    
    // Add shuffle gate - addMGate returns the output wires directly
    std::vector<std::vector<int>> perm;
    std::vector<int> tmp_perm(vec_size);
    #pragma omp parallel for
    for (int i = 0; i < vec_size; ++i) {
        tmp_perm[i] = i;
    }
    perm.push_back(tmp_perm);
    if (pid == 0) {
        for (int i = 1; i < nP; ++i) {
            perm.push_back(tmp_perm);
        }
    }

    std::vector<common::utils::wire_t> shuffled_wires1 = circ.addMGate(common::utils::GateType::kShuffle, input_wires, perm, 0);
    std::vector<common::utils::wire_t> shuffled_wires2 = circ.addMGate(common::utils::GateType::kShuffle, shuffled_wires1, perm, 0);

    // Simulate sorting
    std::vector<common::utils::wire_t> sort_wires1 = circ.addMGate(common::utils::GateType::kShuffle, shuffled_wires1, perm, 0);
    std::vector<common::utils::wire_t> sort_wires2 = circ.addMGate(common::utils::GateType::kShuffle, shuffled_wires2, perm, 0);

    // Current working wires start as shuffled wires
    std::vector<common::utils::wire_t> current_wires;
    current_wires.insert(current_wires.end(), sort_wires1.begin(), sort_wires1.end());
    current_wires.insert(current_wires.end(), sort_wires2.begin(), sort_wires2.end());

    // // Perform vec_size comparisons only once
    std::cout << "Adding " << vec_size << " comparisons" << std::endl;
    
    // // Perform vec_size comparisons (each addGate returns output wire directly)
    std::vector<common::utils::wire_t> comparison_outputs(2*vec_size);
    for (size_t i = 0; i < 2*vec_size; ++i) {
        size_t idx1 = i;
        size_t idx2 = (i + 1) % (2*vec_size); // Compare with next element (wrap around)

        // Compare current_wires[idx1] and current_wires[idx2]
        // Compute diff = current_wires[idx1] - current_wires[idx2]
        auto diff = circ.addGate(common::utils::GateType::kSub, current_wires[idx1], current_wires[idx2]);

        // Compute comparison result: cmp = (diff < 0)
        auto cmp = circ.addGate(common::utils::GateType::kLtz, diff);

        comparison_outputs[i] = cmp;
    }
    
    // current_wires = comparison_outputs;
    
    // Set outputs
    for (size_t i = 0; i < 2* vec_size; ++i) {
        circ.setAsOutput(comparison_outputs[i]);
    }
    
    std::cout << "Sorting circuit generation complete" << std::endl;
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
    auto latency = opts["latency"].as<double>();
    auto pid = opts["pid"].as<size_t>();
    auto threads = opts["threads"].as<size_t>();
    auto seed = opts["seed"].as<size_t>();
    auto repeat = opts["repeat"].as<size_t>();
    auto port = opts["port"].as<int>();

    omp_set_nested(1);
    if (nP < 10) { omp_set_num_threads(nP); }
    else { omp_set_num_threads(10); }
    std::cout << "Starting init_graphiti benchmark" << std::endl;

    std::shared_ptr<io::NetIOMP> network = nullptr;
    if (opts["localhost"].as<bool>()) {
        network = std::make_shared<io::NetIOMP>(pid, nP + 1, latency, port, nullptr, true);
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
        network = std::make_shared<io::NetIOMP>(pid, nP + 1, latency, port, ip.data(), false);
    }

    // Increase socket buffer sizes to prevent deadlocks with large messages
    increaseSocketBuffers(network.get(), 128 * 1024 * 1024);

    int num_rounds = static_cast<int>(std::ceil(std::log2(vec_size)));
    
    json output_data;
    output_data["details"] = {{"num_parties", nP},
                              {"vec_size", vec_size},
                              {"num_comparison_rounds", num_rounds},
                              {"comparisons_executed_per_instance", vec_size},
                              {"num_parallel_instances", 2},
                              {"latency (ms)", latency},
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

    StatsPoint start(*network);

    network->sync();
    StatsPoint init_start(*network);
    auto circ = generateInitCircuit(network, nP, pid, vec_size).orderGatesByLevel();
    network->sync();
    StatsPoint init_end(*network);

    std::cout << "--- Circuit ---" << std::endl;
    std::cout << circ << std::endl;
    
    std::unordered_map<common::utils::wire_t, int> input_pid_map;
    for (const auto& g : circ.gates_by_level[0]) {
        if (g->type == common::utils::GateType::kInp) {
            input_pid_map[g->out] = 1; // All inputs owned by party 1
        }
    }

    std::cout << "Starting preprocessing" << std::endl;
    StatsPoint preproc_start(*network);
    emp::PRG prg(&emp::zero_block, seed);
    OfflineEvaluator off_eval(nP, pid, network, circ, threads, seed);
    auto preproc = off_eval.run(input_pid_map);
    std::cout << "Preprocessing complete" << std::endl;
    network->sync();
    StatsPoint preproc_end(*network);

    std::cout << "Starting online evaluation" << std::endl;
    OnlineEvaluator eval(nP, pid, network, std::move(preproc), circ, threads, seed);
    eval.setRandomInputs();
    
    // Benchmark two sequential shuffles
    network->sync();
    StatsPoint shuffle_start(*network);
    std::cout << "Evaluating initialization (input gates)" << std::endl;
    eval.evaluateGatesAtDepth(0); // Input gates
    std::cout << "Evaluating initialization (first shuffle)" << std::endl;
    eval.evaluateGatesAtDepth(1); // First shuffle
    std::cout << "Evaluating initialization (second shuffle)" << std::endl;
    eval.evaluateGatesAtDepth(2); // Second shuffle
    network->sync();
    StatsPoint shuffle_end(*network);
    
    // Time the two parallel sorting circuits
    network->sync();
    StatsPoint sort_start(*network);
    std::cout << "Evaluating message passing (parallel sorting circuits)" << std::endl;
    for (size_t i = 3; i < circ.gates_by_level.size(); ++i) {
        std::cout << "Evaluating depth " << i << std::endl;
        eval.evaluateGatesAtDepth(i);
    }
    std::cout << "Online evaluation complete" << std::endl;
    network->sync();
    StatsPoint sort_end(*network);
    
    // Calculate benchmarks for initialization
    auto initialization_rbench = shuffle_end - shuffle_start;
    auto sort_rbench = sort_end - sort_start;
    
    double initialization_time = initialization_rbench["time"].get<double>();
    double sort_time = sort_rbench["time"].get<double>();
    
    // we have: shuffle_time + comparison_time for each parallel instance
    // Since they run in parallel, the time is max(instance1, instance2) which is approximately equal
    // The projected time accounts for log(vec_size) rounds of comparisons
    double projected_sort_time = num_rounds * sort_time;
    
    size_t initialization_comm = 0;
    for (const auto& val : initialization_rbench["communication"]) {
        initialization_comm += val.get<int64_t>();
    }
    size_t message_passing_comm = 0;
    for (const auto& val : sort_rbench["communication"]) {
        message_passing_comm += val.get<int64_t>();
    }
    size_t projected_sort_comm = num_rounds * message_passing_comm;
    
    double total_online_time = initialization_time + projected_sort_time;
    size_t total_online_comm = initialization_comm + projected_sort_comm;
    
    StatsPoint end(*network);

    auto init_rbench = init_end - init_start;
    auto preproc_rbench = preproc_end - preproc_start;
    auto total_rbench = end - start;
    
    // Add individual benchmarks
    output_data["benchmarks"].push_back(init_rbench);
    output_data["benchmarks"].push_back(preproc_rbench);
    output_data["benchmarks"].push_back(initialization_rbench);
    output_data["benchmarks"].push_back(sort_rbench);
    output_data["benchmarks"].push_back(total_rbench);
    
    // Add projected runtime
    output_data["projected_runtime"] = {
        {"initialization_time_ms", initialization_time},
        {"sort_time_ms", sort_time},
        {"num_rounds", num_rounds},
        {"projected_sort_time_ms", projected_sort_time},
        {"projected_total_online_time_ms", total_online_time},
        {"initialization_comm_bytes", initialization_comm},
        {"message_passing_comm_bytes", message_passing_comm},
        {"projected_sort_comm_bytes", projected_sort_comm},
        {"projected_total_online_comm_bytes", total_online_comm}
    };

    size_t init_bytes_sent = 0;
    for (const auto& val : init_rbench["communication"]) {
        init_bytes_sent += val.get<int64_t>();
    }
    size_t pre_bytes_sent = 0;
    for (const auto& val : preproc_rbench["communication"]) {
        pre_bytes_sent += val.get<int64_t>();
    }
    size_t total_bytes_sent = 0;
    for (const auto& val : total_rbench["communication"]) {
        total_bytes_sent += val.get<int64_t>();
    }

    std::cout << "--- Benchmark Results ---" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "preproc time: " << preproc_rbench["time"] << " ms" << std::endl;
    std::cout << "preproc sent: " << pre_bytes_sent << " bytes" << std::endl;
    std::cout << "shuffle time (2 sequential shuffles): " << initialization_time << " ms" << std::endl;
    std::cout << "shuffle sent: " << initialization_comm << " bytes" << std::endl;
    std::cout << "sorting time (2 parallel sorting, 1 round): " << sort_time << " ms" << std::endl;
    std::cout << "sorting sent: " << message_passing_comm << " bytes" << std::endl;
    std::cout << "projected init time (" << num_rounds << " rounds): " << projected_sort_time << " ms" << std::endl;
    std::cout << "projected init comm (" << num_rounds << " rounds): " << projected_sort_comm << " bytes" << std::endl;
    std::cout << "online time: " << total_online_time << " ms" << std::endl;
    std::cout << "online sent: " << total_online_comm << " bytes" << std::endl;
    std::cout << "total time: " << total_rbench["time"] << " ms" << std::endl;
    std::cout << "total sent: " << total_bytes_sent << " bytes" << std::endl;
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
        ("vec-size,v", bpo::value<size_t>()->required(), "Size of vector for each sorting instance.")
        ("latency,l", bpo::value<double>()->default_value(100.0), "Network latency in ms.")
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
    bpo::options_description cmdline("Benchmark init_graphiti: 2 sequential shuffles + 2 parallel sorting circuits.");
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
