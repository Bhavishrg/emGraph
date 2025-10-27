#include <io/netmp.h>
#include <emgraph/offline_evaluator.h>
#include <emgraph/online_evaluator.h>
#include <utils/circuit.h>

#include <algorithm>
#include <boost/program_options.hpp>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <omp.h>

#include "utils.h"

using namespace emgraph;
using json = nlohmann::json;
namespace bpo = boost::program_options;

// This benchmark combines the initialization measured in initialization_graphiti
// and one iteration of the message-passing MPA (as implemented in mpa_graphiti).
// It reports:
//   - initialization_time (ms)
//   - message_passing_time (ms) for one iteration
//   - benchmark = initialization_time + iter * message_passing_time

common::utils::Circuit<Ring> generateInitCircuit(std::shared_ptr<io::NetIOMP> &network, int nP, int pid, size_t vec_size) {
    // Reuse the initialization circuit generation from initialization_graphiti
    std::cout << "Generating init_graphiti circuit (for e2e_graphiti)" << std::endl;
    common::utils::Circuit<Ring> circ;

    // Create input wires for two instances (2 * vec_size total)
    std::vector<common::utils::wire_t> input_wires_1(vec_size);
    std::vector<common::utils::wire_t> input_wires_2(vec_size);
    
    std::generate(input_wires_1.begin(), input_wires_1.end(), [&]() { return circ.newInputWire(); });
    std::generate(input_wires_2.begin(), input_wires_2.end(), [&]() { return circ.newInputWire(); });

    // First sequential shuffle
    std::vector<std::vector<int>> perm1;
    std::vector<int> tmp_perm1(vec_size);
    for (int i = 0; i < (int)vec_size; ++i) tmp_perm1[i] = i;
    perm1.push_back(tmp_perm1);
    if (pid == 0) {
        for (int i = 1; i < nP; ++i) perm1.push_back(tmp_perm1);
    }
    std::vector<common::utils::wire_t> shuffled_wires_1 = circ.addMGate(common::utils::GateType::kShuffle, input_wires_1, perm1, 0);

    // Second sequential shuffle
    std::vector<std::vector<int>> perm2;
    std::vector<int> tmp_perm2(vec_size);
    for (int i = 0; i < (int)vec_size; ++i) tmp_perm2[i] = i;
    perm2.push_back(tmp_perm2);
    if (pid == 0) {
        for (int i = 1; i < nP; ++i) perm2.push_back(tmp_perm2);
    }
    std::vector<common::utils::wire_t> shuffled_wires_2 = circ.addMGate(common::utils::GateType::kShuffle, input_wires_2, perm2, 0);

    // Two parallel sorting subcircuits (each has its own shuffle + comparisons)
    // This matches the structure from initialization_graphiti.cpp
    auto addSortingSubcircuit = [&](std::vector<common::utils::wire_t>& input_wires){
        // Add shuffle gate for this sorting instance
        std::vector<std::vector<int>> perm;
        std::vector<int> tmp_perm(input_wires.size());
        for (size_t i = 0; i < input_wires.size(); ++i) {
            tmp_perm[i] = i;
        }
        perm.push_back(tmp_perm);
        if (pid == 0) {
            for (int i = 1; i < nP; ++i) {
                perm.push_back(tmp_perm);
            }
        }
        std::vector<common::utils::wire_t> shuffled_wires = circ.addMGate(common::utils::GateType::kShuffle, input_wires, perm, 0);

        // Perform vec_size comparisons
        std::vector<common::utils::wire_t> comparison_outputs(input_wires.size());
        for (size_t i = 0; i < input_wires.size(); ++i) {
            size_t idx1 = i;
            size_t idx2 = (i + 1) % input_wires.size();
            auto diff = circ.addGate(common::utils::GateType::kSub, shuffled_wires[idx1], shuffled_wires[idx2]);
            auto cmp = circ.addGate(common::utils::GateType::kLtz, diff);
            comparison_outputs[i] = cmp;
        }
        for (auto &w : comparison_outputs) circ.setAsOutput(w);
    };

    addSortingSubcircuit(shuffled_wires_1);
    addSortingSubcircuit(shuffled_wires_2);

    return circ;
}

common::utils::Circuit<Ring> generateMPACircuit(std::shared_ptr<io::NetIOMP> &network, int nP, int pid, size_t vec_size, int iter) {
    // Reuse the main MPA circuit generator from mpa_graphiti but produce a single-iteration circuit
    std::cout << "Generating mpa_graphiti circuit (for e2e_graphiti)" << std::endl;
    common::utils::Circuit<Ring> circ;

    size_t num_vert = 0.1 * vec_size;
    size_t num_edge = vec_size - num_vert;
    int n = nP;

    std::vector<common::utils::wire_t> full_vertex_list(num_vert);
    for (size_t i = 0; i < num_vert; ++i) full_vertex_list[i] = circ.newInputWire();

    std::vector<std::vector<common::utils::wire_t>> subg_edge_list(n);
    // distribute edges roughly equally
    for (int p = 0; p < n; ++p) {
        size_t edges_for_p = num_edge / n + (p == n-1 ? num_edge % n : 0);
        subg_edge_list[p].resize(edges_for_p);
        for (size_t j = 0; j < edges_for_p; ++j) subg_edge_list[p][j] = circ.newInputWire();
    }

    // Keep permutations trivial (identity) to focus on measuring gate evaluation and communication patterns
    std::vector<std::vector<int>> pub_perm_g(n), pub_perm_s(n), pub_perm_d(n), pub_perm_v(n);
    for (int p = 0; p < n; ++p) {
        pub_perm_g[p].resize(num_vert / n + (p == n-1 ? num_vert % n : 0));
        for (size_t i = 0; i < pub_perm_g[p].size(); ++i) pub_perm_g[p][i] = i;
        pub_perm_s[p].resize(subg_edge_list[p].size());
        for (size_t i = 0; i < pub_perm_s[p].size(); ++i) pub_perm_s[p][i] = i;
        pub_perm_d[p] = pub_perm_s[p];
        pub_perm_v[p] = pub_perm_s[p];
    }

    // Perform a single iteration of the MPA-like message passing (decompose, apply, combine)
    // For simplicity, we build a skeleton that mirrors the steps but keeps gates simple.
    // DECOMPOSE: permute (we use identity)
    std::vector<std::vector<common::utils::wire_t>> subg_sorted(n);
    // split full_vertex_list among parties
    size_t base = num_vert / n;
    size_t rem = num_vert % n;
    size_t idx = 0;
    for (int p = 0; p < n; ++p) {
        size_t count = base + (p == n-1 ? rem : 0);
        subg_sorted[p].reserve(count);
        for (size_t j = 0; j < count; ++j) {
            subg_sorted[p].push_back(full_vertex_list[idx++]);
        }
    }

    // For each party, create simple propagate/applyv/gather operations
    for (int p = 0; p < n; ++p) {
        // PROPAGATE (pairwise subtractions)
        std::vector<common::utils::wire_t> prop(subg_sorted[p].size());
        for (int j = (int)subg_sorted[p].size()-1; j > 0; --j) {
            prop[j] = circ.addGate(common::utils::GateType::kSub, subg_sorted[p][j], subg_sorted[p][j-1]);
        }
        // APPLYV: small const mul/add sequence
        std::vector<common::utils::wire_t> applyv(prop.size());
        for (size_t j = 0; j < prop.size(); ++j) {
            auto pgr = circ.addConstOpGate(common::utils::GateType::kConstMul, prop[j], Ring(1));
            applyv[j] = circ.addConstOpGate(common::utils::GateType::kConstAdd, pgr, Ring(1));
        }
        // COMBINE: take first few into output
        for (size_t j = 0; j < applyv.size(); ++j) circ.setAsOutput(applyv[j]);
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

    auto nP = opts["num-parties"].as<int>();
    auto vec_size = opts["vec-size"].as<size_t>();
    auto iter = opts["iter"].as<int>();
    auto latency = opts["latency"].as<double>();
    auto pid = opts["pid"].as<size_t>();
    auto threads = opts["threads"].as<size_t>();
    auto seed = opts["seed"].as<size_t>();
    auto repeat = opts["repeat"].as<size_t>();
    auto port = opts["port"].as<int>();

    omp_set_nested(1);
    if (nP < 10) { omp_set_num_threads(nP); }
    else { omp_set_num_threads(10); }
    std::cout << "Starting e2e_graphiti benchmark" << std::endl;

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

    json output_data;
    output_data["details"] = {{"num_parties", nP},
                              {"vec_size", vec_size},
                              {"iterations", iter},
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

    // Initialization circuit and timing
    network->sync();
    StatsPoint init_start(*network);
    auto init_circ = generateInitCircuit(network, nP, pid, vec_size).orderGatesByLevel();
    network->sync();
    StatsPoint init_end(*network);

    int latency_ms = static_cast<int>(latency);  // Convert latency from double to int milliseconds

    std::cout << "--- Init Circuit ---" << std::endl;
    std::cout << init_circ << std::endl;

    // Preprocessing for init circuit
    std::unordered_map<common::utils::wire_t, int> input_pid_map;
    for (const auto& g : init_circ.gates_by_level[0]) {
        if (g->type == common::utils::GateType::kInp) input_pid_map[g->out] = 1;
    }
    std::cout << "Starting preprocessing (init)" << std::endl;
    StatsPoint preproc_init_start(*network);
    emp::PRG prg(&emp::zero_block, seed);
    OfflineEvaluator off_eval_init(nP, pid, network, init_circ, threads, seed, latency_ms);
    auto preproc_init = off_eval_init.run(input_pid_map);
    std::cout << "Preprocessing complete (init)" << std::endl;
    network->sync();
    StatsPoint preproc_init_end(*network);

    // Online evaluate initialization (two sequential shuffles ONLY)
    // The two parallel sorting circuits come after initialization
    std::cout << "Starting online evaluation (init - two sequential shuffles)" << std::endl;
    OnlineEvaluator eval_init(nP, pid, network, std::move(preproc_init), init_circ, threads, seed, latency_ms);
    eval_init.setRandomInputs();
    network->sync();
    StatsPoint shuffle_start(*network);
    // Evaluate initialization phase: inputs + first shuffle + second shuffle
    std::cout << "Evaluating initialization (input gates)" << std::endl;
    eval_init.evaluateGatesAtDepth(0); // Input gates
    std::cout << "Evaluating initialization (first shuffle)" << std::endl;
    eval_init.evaluateGatesAtDepth(1); // First shuffle
    std::cout << "Evaluating initialization (second shuffle)" << std::endl;
    eval_init.evaluateGatesAtDepth(2); // Second shuffle
    network->sync();
    StatsPoint shuffle_end(*network);
    
    // Now evaluate the two parallel sorting circuits (the sorting phase)
    network->sync();
    StatsPoint sort_start(*network);
    std::cout << "Evaluating sorting phase (two parallel sorting circuits)" << std::endl;
    for (size_t i = 3; i < init_circ.gates_by_level.size(); ++i) {
        std::cout << "Evaluating depth " << i << std::endl;
        eval_init.evaluateGatesAtDepth(i);
    }
    network->sync();
    StatsPoint sort_end(*network);

    auto init_rbench = init_end - init_start;
    auto preproc_init_rbench = preproc_init_end - preproc_init_start;
    auto initialization_rbench = shuffle_end - shuffle_start;
    auto sort_rbench = sort_end - sort_start;

    double initialization_time = initialization_rbench["time"].get<double>();
    size_t initialization_comm = 0;
    for (const auto& val : initialization_rbench["communication"]) initialization_comm += val.get<int64_t>();
    
    double sort_time = sort_rbench["time"].get<double>();
    size_t sort_comm = 0;
    for (const auto& val : sort_rbench["communication"]) sort_comm += val.get<int64_t>();
    
    // Total initialization includes both shuffle phase and sorting phase
    double total_init_time = initialization_time + sort_time;
    size_t total_init_comm = initialization_comm + sort_comm;

    // Now generate MPA circuit and measure one iteration of message passing
    network->sync();
    StatsPoint mpa_init_start(*network);
    auto mpa_circ = generateMPACircuit(network, nP, pid, vec_size, 1).orderGatesByLevel();
    network->sync();
    StatsPoint mpa_init_end(*network);

    std::cout << "--- MPA Circuit ---" << std::endl;
    std::cout << mpa_circ << std::endl;

    // Preprocessing for MPA circuit
    std::unordered_map<common::utils::wire_t, int> input_pid_map_m;
    for (const auto& g : mpa_circ.gates_by_level[0]) {
        if (g->type == common::utils::GateType::kInp) input_pid_map_m[g->out] = 1;
    }
    std::cout << "Starting preprocessing (mpa)" << std::endl;
    StatsPoint preproc_mpa_start(*network);
    OfflineEvaluator off_eval_mpa(nP, pid, network, mpa_circ, threads, seed, latency_ms);
    auto preproc_mpa = off_eval_mpa.run(input_pid_map_m);
    std::cout << "Preprocessing complete (mpa)" << std::endl;
    network->sync();
    StatsPoint preproc_mpa_end(*network);

    std::cout << "Starting online evaluation (mpa - message passing)" << std::endl;
    OnlineEvaluator eval_mpa(nP, pid, network, std::move(preproc_mpa), mpa_circ, threads, seed, latency_ms);
    eval_mpa.setRandomInputs();
    network->sync();
    StatsPoint mpa_start(*network);
    // Evaluate the full circuit which models one iteration's message passing
    for (size_t i = 0; i < mpa_circ.gates_by_level.size(); ++i) eval_mpa.evaluateGatesAtDepth(i);
    network->sync();
    StatsPoint mpa_end(*network);

    auto mpa_rbench = mpa_end - mpa_start;
    auto mpa_preproc_rbench = preproc_mpa_end - preproc_mpa_start;
    
    double message_passing_time = mpa_rbench["time"].get<double>();
    size_t message_passing_comm = 0;
    for (const auto& val : mpa_rbench["communication"]) message_passing_comm += val.get<int64_t>();

    double preproc_init_time = preproc_init_rbench["time"].get<double>();
    size_t preproc_init_comm = 0;
    for (const auto& val : preproc_init_rbench["communication"]) preproc_init_comm += val.get<int64_t>();

    double preproc_mpa_time = mpa_preproc_rbench["time"].get<double>();
    size_t preproc_mpa_comm = 0;
    for (const auto& val : mpa_preproc_rbench["communication"]) preproc_mpa_comm += val.get<int64_t>();

    double total_preproc_time = preproc_init_time + preproc_mpa_time;
    size_t total_preproc_comm = preproc_init_comm + preproc_mpa_comm;

    // Projected benchmark: total_initialization + iter * message_passing
    double projected_total_online_time = total_init_time + static_cast<double>(iter) * message_passing_time;
    size_t projected_total_comm = total_init_comm + static_cast<size_t>(iter) * message_passing_comm;

    StatsPoint end(*network);

    auto total_rbench = end - start;

    output_data["benchmarks"].push_back(init_rbench);
    output_data["benchmarks"].push_back(preproc_init_rbench);
    output_data["benchmarks"].push_back(initialization_rbench);
    output_data["benchmarks"].push_back(sort_rbench);
    output_data["benchmarks"].push_back(mpa_preproc_rbench);
    output_data["benchmarks"].push_back(mpa_rbench);
    output_data["benchmarks"].push_back(total_rbench);

    output_data["projected_runtime"] = {
        {"preproc_init_time_ms", preproc_init_time},
        {"preproc_mpa_time_ms", preproc_mpa_time},
        {"total_preproc_time_ms", total_preproc_time},
        {"shuffle_time_ms", initialization_time},
        {"sort_time_ms", sort_time},
        {"total_initialization_time_ms", total_init_time},
        {"message_passing_time_ms", message_passing_time},
        {"iterations", iter},
        {"projected_total_online_time_ms", projected_total_online_time},
        {"preproc_init_comm_bytes", preproc_init_comm},
        {"preproc_mpa_comm_bytes", preproc_mpa_comm},
        {"total_preproc_comm_bytes", total_preproc_comm},
        {"shuffle_comm_bytes", initialization_comm},
        {"sort_comm_bytes", sort_comm},
        {"total_initialization_comm_bytes", total_init_comm},
        {"message_passing_comm_bytes", message_passing_comm},
        {"projected_total_comm_bytes", projected_total_comm}
    };

    std::cout << "--- Benchmark Results (e2e_graphiti) ---" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "preproc init time (ms): " << preproc_init_time << std::endl;
    std::cout << "preproc init comm (bytes): " << preproc_init_comm << std::endl;
    std::cout << "preproc mpa time (ms): " << preproc_mpa_time << std::endl;
    std::cout << "preproc mpa comm (bytes): " << preproc_mpa_comm << std::endl;
    std::cout << "preproc time: " << total_preproc_time << " ms" << std::endl;
    std::cout << "preproc sent: " << total_preproc_comm << " bytes" << std::endl;
    std::cout << "shuffle time (2 sequential shuffles, ms): " << initialization_time << std::endl;
    std::cout << "shuffle comm (bytes): " << initialization_comm << std::endl;
    std::cout << "sort time (2 parallel sorts, ms): " << sort_time << std::endl;
    std::cout << "sort comm (bytes): " << sort_comm << std::endl;
    std::cout << "total initialization time (ms): " << total_init_time << std::endl;
    std::cout << "total initialization comm (bytes): " << total_init_comm << std::endl;
    std::cout << "message passing time (1 iter, ms): " << message_passing_time << std::endl;
    std::cout << "message passing comm (bytes): " << message_passing_comm << std::endl;
    // std::cout << "projected total online time (ms): " << projected_total_online_time << std::endl;
    // std::cout << "projected total comm (bytes): " << projected_total_comm << std::endl;
    std::cout << "online time: " << projected_total_online_time << " ms" << std::endl;
    std::cout << "online sent: " << projected_total_comm << " bytes" << std::endl;
    

    output_data["stats"] = {{"peak_virtual_memory", peakVirtualMemory()},
                            {"peak_resident_set_size", peakResidentSetSize()}};

    if (save_output) saveJson(output_data, save_file);
}

// clang-format off
bpo::options_description programOptions() {
    bpo::options_description desc("Following options are supported by config file too.");
    desc.add_options()
        ("num-parties,n", bpo::value<int>()->required(), "Number of parties.")
        ("vec-size,v", bpo::value<size_t>()->required(), "Size of vector for each sorting/MPA instance.")
        ("iter,i", bpo::value<int>()->default_value(1), "Number of iterations for message passing.")
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
    bpo::options_description cmdline("Benchmark e2e_graphiti: initialization + iter * message_passing.");
    cmdline.add(prog_opts);
    cmdline.add_options()("config,c", bpo::value<std::string>(), "configuration file for easy specification of cmd line arguments")("help,h", "produce help message");
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
