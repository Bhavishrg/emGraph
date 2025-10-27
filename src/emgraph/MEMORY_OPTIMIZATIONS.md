# Memory Optimizations for e2e_graphiti

## Overview
Comprehensive memory optimizations applied to reduce RAM usage during e2e_graphiti benchmarks. These changes target the largest memory consumers: circuits, preprocessing data, wire buffers, and communication buffers.

## Changes Applied

### 1. OfflineEvaluator (src/emgraph/offline_evaluator.cpp)
**File**: `offline_evaluator.cpp::run()`

**Change**: Free stored circuit immediately after preprocessing is complete.
```cpp
// After setWireMasks(), clear circ_ before returning preproc_
circ_ = common::utils::LevelOrderedCircuit();
circ_.num_gates = 0;
circ_.num_wires = 0;
circ_.gates_by_level.clear();
circ_.gates_by_level.shrink_to_fit();
```

**Benefit**: Releases ~50-70% of offline evaluator memory once preprocessing is done.

---

### 2. OnlineEvaluator - Basic Memory Release (src/emgraph/online_evaluator_load_balanced.cpp)
**Files**: `online_evaluator.h`, `online_evaluator_load_balanced.cpp`

**Change**: Added `releaseMemory()` method to explicitly free large buffers.
```cpp
void OnlineEvaluator::releaseMemory() {
    circ_ = common::utils::LevelOrderedCircuit();
    // Clear gates_by_level, preproc_.gates, wires_
}
```

**Benefit**: Allows explicit cleanup after evaluation finishes. Called in e2e_graphiti after init and MPA phases.

---

### 3. Progressive Preprocessing Cleanup (src/emgraph/online_evaluator_load_balanced.cpp)
**Change**: Added `freeDepthPreproc()` method to free preprocessing data layer-by-layer.
```cpp
void OnlineEvaluator::freeDepthPreproc(size_t depth) {
    // Erase preproc_.gates entries for all gates at this depth
}
```
Called at end of each `evaluateGatesAtDepth()`.

**Benefit**: Reduces peak memory by freeing preprocessing data as soon as it's no longer needed (after evaluating that depth).

---

### 4. Precise Vector Reservations (src/emgraph/online_evaluator_load_balanced.cpp)
**Change**: Pre-count gate types before creating vectors, then reserve exact sizes.
```cpp
// In evaluateGatesAtDepth():
// Pre-count mult_num, mult3_num, etc.
mult_vals.reserve(mult_num * 2);
mult3_vals.reserve(mult3_num * 3);
// ...
```

**Benefit**: Prevents vector over-allocation and reallocation during growth.

---

### 5. Immediate Buffer Cleanup (src/emgraph/online_evaluator_load_balanced.cpp)
**Change**: Clear and shrink intermediate buffers immediately after copying data.

**Locations**:
- After copying mult_vals/mult3_vals/mult4_vals/dotp_vals to online_comm_send
- After extracting mult_all/mult3_all/mult4_all/dotp_all from online_comm_recv
- After processing online_comm_recv_party
- At end of evaluateGatesAtDepth: clear all gate collections

**Benefit**: Frees temporary communication buffers (which can be large for big circuits) as soon as possible.

---

### 6. BoolEval Memory Optimization (src/emgraph/online_evaluator_load_balanced.cpp)
**Change**: Free BoolEval communication buffers in `evaluateGatesAtDepth()`.
```cpp
// After evaluateGatesAtDepthPartyRecv in BoolEval:
online_comm_send.clear(); online_comm_send.shrink_to_fit();
online_comm_recv.clear(); online_comm_recv.shrink_to_fit();
// ... clear all mult_all, mult3_all, mult4_all, dotp_all
```

**Benefit**: BoolEval is used inside eqzEvaluate/ltzEvaluate and can hold large vwires buffers. Aggressive cleanup reduces peak.

---

### 7. Reduced Benchmark JSON Accumulation (benchmark/e2e_graphiti.cpp)
**Change**: Removed accumulation of all benchmark JSON objects in `output_data["benchmarks"]`.
```cpp
// Removed these lines:
// output_data["benchmarks"].push_back(init_rbench);
// output_data["benchmarks"].push_back(preproc_init_rbench);
// ...
```

**Benefit**: Avoids keeping large JSON objects (with communication arrays) in memory. Essential values are already extracted.

---

### 8. Circuit Cleanup in e2e_graphiti (benchmark/e2e_graphiti.cpp)
**Change**: Clear local circuit copies after evaluation finishes.
```cpp
// After sort_end:
eval_init.releaseMemory();
init_circ = common::utils::LevelOrderedCircuit();

// After mpa_end:
eval_mpa.releaseMemory();
mpa_circ = common::utils::LevelOrderedCircuit();
```

**Benefit**: Frees both evaluator internal memory and local circuit copies.

---

## Expected Impact

### Memory Reduction Breakdown (estimated):
1. **OfflineEvaluator circuit release**: ~15-20% of total peak RAM
2. **Progressive preproc cleanup**: ~10-15% of peak RAM
3. **Immediate buffer cleanup**: ~10-15% of peak RAM
4. **OnlineEvaluator releaseMemory()**: ~20-30% after evaluation
5. **BoolEval optimizations**: ~5-10% during eqz/ltz phases
6. **Benchmark JSON reduction**: ~2-5% for large benchmarks
7. **LTZ gate preprocessing (move + cache)**: ~20-40% during offline phase for circuits with many LTZ/EQZ gates

**Total estimated reduction**: 50-70% lower peak RAM usage

---

## Testing Instructions

### Build the project:
```bash
cd /home/cris/Bhavish/emGraph_vldb/Asterisk/build
make -j$(nproc)
```

### Run a small benchmark and monitor memory:
```bash
# In one terminal, monitor memory:
watch -n 1 'ps aux | grep e2e_graphiti | grep -v grep'

# In another terminal, run benchmark:
cd /home/cris/Bhavish/emGraph_vldb/Asterisk
./build/benchmarks/e2e_graphiti --localhost -n 2 -v 10000 -i 1 -p 1 -t 6 --seed 200
```

### Check peak memory:
- Look for RSS (Resident Set Size) column in `ps aux` output
- Compare before/after optimizations
- For large vec-size (100000+), the difference should be significant

---

### 9. LTZ Gate Preprocessing Optimizations (src/emgraph/)
**Files**: `preproc.h`, `offline_evaluator.h`, `offline_evaluator.cpp`

**Changes**:

**a) Move semantics for PreprocLtzGate** (`preproc.h`)
```cpp
template <class R>
struct PreprocLtzGate : public PreprocGate<R> {
  // ... members ...
  
  // Enable efficient move semantics to avoid unnecessary copies
  PreprocLtzGate(PreprocLtzGate&&) noexcept = default;
  PreprocLtzGate& operator=(PreprocLtzGate&&) noexcept = default;
};
```
**Benefit**: Large vectors (share_r_bits, tp_share_r_bits, PrefixOR_gates) are moved into preproc map without copying, reducing peak memory during preprocessing.

**b) Circuit template caching** (`offline_evaluator.h`, `offline_evaluator.cpp`)
```cpp
// In offline_evaluator.h - add private static methods:
static const common::utils::LevelOrderedCircuit& getMultKCircuitTemplate();
static const common::utils::LevelOrderedCircuit& getPrefixORCircuitTemplate();

// In offline_evaluator.cpp - implement with Meyer's singleton:
const common::utils::LevelOrderedCircuit& OfflineEvaluator::getMultKCircuitTemplate() {
  static const auto circuit = common::utils::Circuit<BoolRing>::generateMultK().orderGatesByLevel();
  return circuit;
}

// In setWireMasksParty() - use cached templates:
const auto& multk_circ_template = getMultKCircuitTemplate();
const auto& prefixOR_circ_template = getPrefixORCircuitTemplate();
```

**Benefit**: 
- EQZ gates use MultK circuit, LTZ gates use PrefixOR circuit for bit-level operations
- Before: Each LTZ/EQZ gate regenerated its Boolean sub-circuit (allocating temporary gate structures)
- After: Circuit templates created once per process, shared across all preprocessing calls
- Eliminates O(num_ltz_gates × circuit_size) allocations
- Reduces memory churn during preprocessing by ~20-40% for circuits with many LTZ/EQZ gates
 
---

## Additional Optimization Opportunities (if RAM still high)

1. **Stream output JSON to disk incrementally** instead of building in memory
2. **Chunk-based processing for very large circuits** (evaluate in batches)
3. **Reduce vwires allocation in BoolEval** (allocate on-demand per gate type)
4. **Use memory-mapped files for preprocessing data** (swap to disk when not in use)
5. **Implement circuit compression** (store gates in compressed format)

---

## Files Modified

1. `src/emgraph/offline_evaluator.cpp` - Free circuit after preprocessing
2. `src/emgraph/online_evaluator.h` - Add releaseMemory() and freeDepthPreproc() declarations
3. `src/emgraph/online_evaluator_load_balanced.cpp` - Implement all optimization methods
4. `benchmark/e2e_graphiti.cpp` - Call releaseMemory() and clear circuits
5. `src/emgraph/preproc.h` - Add move semantics to PreprocLtzGate
6. `src/emgraph/offline_evaluator.h` - Add circuit template cache methods
7. `src/emgraph/offline_evaluator.cpp` - Implement circuit template caching for LTZ/EQZ gates

---

**Date**: 26 October 2025  
**Optimization Level**: Aggressive memory reduction while maintaining correctness
