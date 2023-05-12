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

common::utils::Circuit<Field> PSL_PostLTZ_Circuit(int N) {
    common::utils::Circuit<Field> circ;
    std::vector<common::utils::wire_t> inp(2 * N);
    std::vector<common::utils::wire_t> level1(2 * N);
    std::vector<common::utils::wire_t> level2(N);
    std::vector<common::utils::wire_t> level3(N);
    for(size_t i = 0; i < 2 * N; i++) {
        inp[i] = circ.newInputWire();
    }
    for(size_t i = 0; i < (2 * N) - 1; i++) {
        level1[i] = circ.addGate(common::utils::GateType::kMul, inp[i], inp[i+1]);
    }
    level1[(2 * N)-1] = circ.addGate(common::utils::GateType::kMul, inp[N-1], inp[0]);

    for(size_t i = 0; i < N; i++) {
        level2[i] = circ.addGate(common::utils::GateType::kMul, level1[i], level1[i+1]);
    }
    
    for(size_t i = 0; i < N-1; i++) {
        level3[i] = circ.addGate(common::utils::GateType::kMul, level2[i], level2[i+1]);
    }
    level3[N-1] = circ.addGate(common::utils::GateType::kMul, level2[N-1], level2[0]);

    return circ;
}

common::utils::Circuit<Field> ObSel_Circuit() {
    common::utils::Circuit<Field> circ;
    common::utils::wire_t wa = circ.newInputWire();
    common::utils::wire_t wb = circ.newInputWire();
    common::utils::wire_t wc = circ.newInputWire();
    common::utils::wire_t wd = circ.addGate(common::utils::GateType::kMul, wa, wb);
    common::utils::wire_t we = circ.addGate(common::utils::GateType::kMul, wc, wd);
    return circ;
}

common::utils::Circuit<Field> INSERT_PostLTZ_Circuit(int M) {
    common::utils::Circuit<Field> circ;
    std::vector<common::utils::wire_t> inp(M + 2);
    std::vector<common::utils::wire_t> level1(M + 2);
    std::vector<common::utils::wire_t> level2(M + 1);
    std::vector<common::utils::wire_t> level3(M + 1);
    std::vector<common::utils::wire_t> level4(M + 1);
    std::vector<common::utils::wire_t> temp1(M + 2);
    std::vector<common::utils::wire_t> temp2(M + 1);
    std::vector<common::utils::wire_t> temp3(M + 1);
    std::vector<common::utils::wire_t> temp4(M + 1);
    
    for(size_t i = 0; i < M + 2; i++) {
        inp[i] = circ.newInputWire();
    }
    
    for(size_t i = 0; i < M + 1; i++) {
        level1[i] = circ.addGate(common::utils::GateType::kMul, inp[i], inp[i + 1]);
    }
    level1[M + 1] = circ.addGate(common::utils::GateType::kMul, inp[M + 1], inp[0]);
    
    for(size_t i = 0; i < M + 1; i++) {
        level2[i] = circ.addGate(common::utils::GateType::kMul, level1[i], level1[i+1]);
    }
    
    for(size_t i = 0; i < M; i++) {
        level3[i] = circ.addGate(common::utils::GateType::kMul, level2[i], level2[i+1]);
    }
    level3[M] = circ.addGate(common::utils::GateType::kMul, level2[M], level2[0]);
    
    for(size_t i = 0; i < M; i++) {
        
        temp1[i] = circ.addGate(common::utils::GateType::kMul,level1[i], inp[i]);
        temp2[i] = circ.addGate(common::utils::GateType::kMul,level2[i], inp[0]);
        temp3[i] = circ.addGate(common::utils::GateType::kMul,level3[i], inp[0]);
        temp4[i] = circ.addGate(common::utils::GateType::kAdd, temp1[i], temp2[i]);
        level4[i] = circ.addGate(common::utils::GateType::kAdd, temp3[i], temp4[i]);
    }
    temp1[M] = circ.addGate(common::utils::GateType::kMul,level1[M], inp[M]);
    temp2[M] = circ.addGate(common::utils::GateType::kMul,level2[M], inp[0]);
    temp3[M] = circ.addGate(common::utils::GateType::kMul,level3[M], inp[M-1]);
    temp4[M] = circ.addGate(common::utils::GateType::kAdd, temp1[M], temp2[M]);
    level4[M] = circ.addGate(common::utils::GateType::kAdd, temp3[M], temp4[M]);

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
    

    // PSL
    // 2N LTZ + 2N Bit2A + N Mult + k rec + 1 ObSel (1 Mult)
    auto PSL_LTZ_circ = common::utils::Circuit<BoolRing>::generateParaPrefixAND(2 * N).orderGatesByLevel();
    auto PSL_postLTZ_circ = PSL_PostLTZ_Circuit(N).orderGatesByLevel();
    
    // EQZ
    auto EQZ_circ = common::utils::Circuit<BoolRing>::generateMultK().orderGatesByLevel();
    
    // ObSel
    auto ObSel_circ = ObSel_Circuit().orderGatesByLevel();
    
    // Insert
    // M+2 LTZ + M+2 Mult + M+1 Mult + M+1 Mult + M+1(Mult + Add + Mult + Add + Mult)
    auto INSERT_LTZ_circ = common::utils::Circuit<BoolRing>::generateParaPrefixAND(M + 2).orderGatesByLevel();
    auto INSERT_postLTZ_circ = INSERT_PostLTZ_Circuit(M).orderGatesByLevel();

    std::unordered_map<common::utils::wire_t, int> PSL_LTZ_input_pid_map;
    std::unordered_map<common::utils::wire_t, BoolRing> PSL_LTZ_input_map;
    std::unordered_map<common::utils::wire_t, BoolRing> PSL_LTZ_bit_mask_map;
    std::vector<AuthAddShare<BoolRing>> PSL_LTZ_output_mask;
    std::vector<TPShare<BoolRing>> PSL_LTZ_output_tpmask;

    std::unordered_map<common::utils::wire_t, int> PSL_postLTZ_input_pid_map;
    std::unordered_map<common::utils::wire_t, Field> PSL_postLTZ_input_map;

    std::unordered_map<common::utils::wire_t, int> EQZ_input_pid_map;
    std::unordered_map<common::utils::wire_t, BoolRing> EQZ_input_map;
    std::unordered_map<common::utils::wire_t, BoolRing> EQZ_bit_mask_map;
    std::vector<AuthAddShare<BoolRing>> EQZ_output_mask;
    std::vector<TPShare<BoolRing>> EQZ_output_tpmask;


    std::unordered_map<common::utils::wire_t, int> ObSel_input_pid_map;
    std::unordered_map<common::utils::wire_t, Field> ObSel_input_map;

    std::unordered_map<common::utils::wire_t, int> INSERT_LTZ_input_pid_map;
    std::unordered_map<common::utils::wire_t, BoolRing> INSERT_LTZ_input_map;
    std::unordered_map<common::utils::wire_t, BoolRing> INSERT_LTZ_bit_mask_map;
    std::vector<AuthAddShare<BoolRing>> INSERT_LTZ_output_mask;
    std::vector<TPShare<BoolRing>> INSERT_LTZ_output_tpmask;


    std::unordered_map<common::utils::wire_t, int> INSERT_postLTZ_input_pid_map;
    std::unordered_map<common::utils::wire_t, Field> INSERT_postLTZ_input_map;
    
    for (const auto& g : PSL_LTZ_circ.gates_by_level[0]) {
        if (g->type == common::utils::GateType::kInp) {
            PSL_LTZ_input_pid_map[g->out] = 1;
            PSL_LTZ_input_map[g->out] = 1;
            PSL_LTZ_bit_mask_map[g->out] = 0;
        }
    }
    for (const auto& g : PSL_postLTZ_circ.gates_by_level[0]) {
        if (g->type == common::utils::GateType::kInp) {
            PSL_postLTZ_input_pid_map[g->out] = 1;
            PSL_postLTZ_input_map[g->out] = 1;
        }
    }
    for (const auto& g : EQZ_circ.gates_by_level[0]) {
        if (g->type == common::utils::GateType::kInp) {
            EQZ_input_pid_map[g->out] = 1;
            EQZ_input_map[g->out] = 1;
            EQZ_bit_mask_map[g->out] = 0;
        }
    }
    for (const auto& g : ObSel_circ.gates_by_level[0]) {
        if (g->type == common::utils::GateType::kInp) {
            ObSel_input_pid_map[g->out] = 1;
            ObSel_input_map[g->out] = 1;
        }
    }
    for (const auto& g : INSERT_LTZ_circ.gates_by_level[0]) {
        if (g->type == common::utils::GateType::kInp) {
            INSERT_LTZ_input_pid_map[g->out] = 1;
            INSERT_LTZ_input_map[g->out] = 1;
            INSERT_LTZ_bit_mask_map[g->out] = 0;
        }
    }
    for (const auto& g : INSERT_postLTZ_circ.gates_by_level[0]) {
        if (g->type == common::utils::GateType::kInp) {
            INSERT_postLTZ_input_pid_map[g->out] = 1;
            INSERT_postLTZ_input_map[g->out] = 1;
        }
    }

     
    emp::PRG prg(&emp::zero_block, seed);

    for (size_t r = 0; r < repeat; ++r) {
        StatsPoint start(*network);

        // Offline 
        OfflineBoolEvaluator PSL_LTZ_off_eval(nP, pid, network, PSL_LTZ_circ, seed);
        OfflineEvaluator PSL_postLTZ_off_eval(nP, pid, network, PSL_postLTZ_circ, security_param, threads, seed);
        
        OfflineBoolEvaluator EQZ_off_eval(nP, pid, network, EQZ_circ, seed);

        OfflineEvaluator ObSel_off_eval(nP, pid, network, ObSel_circ, security_param, threads, seed);

        OfflineBoolEvaluator INSERT_LTZ_off_eval(nP, pid, network, INSERT_LTZ_circ, seed);
        OfflineEvaluator INSERT_postLTZ_off_eval(nP, pid, network, INSERT_postLTZ_circ, security_param, threads, seed);

        auto PSL_LTZ_preproc = PSL_LTZ_off_eval.run(PSL_LTZ_input_pid_map, PSL_LTZ_bit_mask_map, PSL_LTZ_output_mask, PSL_LTZ_output_tpmask);
        auto PSL_postLTZ_preproc = PSL_postLTZ_off_eval.run(PSL_postLTZ_input_pid_map);
        auto EQZ_preproc = EQZ_off_eval.run(EQZ_input_pid_map, EQZ_bit_mask_map, EQZ_output_mask, EQZ_output_tpmask);
        auto ObSel_preproc = ObSel_off_eval.run(ObSel_input_pid_map);
        auto INSERT_LTZ_preproc = INSERT_LTZ_off_eval.run(INSERT_LTZ_input_pid_map, INSERT_LTZ_bit_mask_map, INSERT_LTZ_output_mask, INSERT_LTZ_output_tpmask);
        auto INSERT_postLTZ_preproc = INSERT_postLTZ_off_eval.run(INSERT_postLTZ_input_pid_map);

        // Online
        BoolEvaluator PSL_LTZ_eval(nP, pid, network, std::move(PSL_LTZ_preproc), PSL_LTZ_circ, seed);
        OnlineEvaluator PSL_postLTZ_eval(nP, pid, network, std::move(PSL_postLTZ_preproc), PSL_postLTZ_circ, 
                    security_param, threads, seed);

        BoolEvaluator EQZ_eval(nP, pid, network, std::move(EQZ_preproc), EQZ_circ, seed);

        OnlineEvaluator ObSel_eval(nP, pid, network, std::move(ObSel_preproc), ObSel_circ, 
                    security_param, threads, seed);

        BoolEvaluator INSERT_LTZ_eval(nP, pid, network, std::move(INSERT_LTZ_preproc), INSERT_LTZ_circ, seed);
        OnlineEvaluator INSERT_postLTZ_eval(nP, pid, network, std::move(INSERT_postLTZ_preproc), INSERT_postLTZ_circ, 
                    security_param, threads, seed);

        auto PSL_LTZ_res = PSL_LTZ_eval.evaluateCircuit(PSL_LTZ_input_map);
        auto PSL_postLTZ_res = PSL_postLTZ_eval.evaluateCircuit(PSL_postLTZ_input_map);
        auto EQZ_res = EQZ_eval.evaluateCircuit(EQZ_input_map);
        auto ObSel_res = ObSel_eval.evaluateCircuit(ObSel_input_map);
        auto INSERT_LTZ_res = PSL_LTZ_eval.evaluateCircuit(PSL_LTZ_input_map);
        auto INSERT_postLTZ_res = PSL_postLTZ_eval.evaluateCircuit(PSL_postLTZ_input_map);


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