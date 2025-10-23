#!/bin/bash
# set -x

# Usage: ./run.sh <benchmark_name> [benchmark_options...]
# Example: ./run.sh grouppropagate -l 0.5 --t1-size 8 --t2-size 10
# Example: ./run.sh reconstruction -l 0.5 -i 10 --use-pking true
# Example: ./run.sh mult -l 100.0 -i 30

if [ $# -lt 1 ]; then
    echo "Usage: $0 <benchmark_name> [benchmark_options...]"
    echo ""
    echo "Available benchmarks:"
    echo "  - e2e_emgraph"
    echo "  - e2e_graphiti"
    echo "  - mpa_emgraph"
    echo "  - mpa_graphiti"
    echo ""
    echo "Example: $0 mpa_emgraph -l 0.5 --vec-size 10000 -i 10"
    exit 1
fi

# Get benchmark name from first argument
BENCHMARK_NAME="$1"
shift  # Remove first argument, leaving only the options

# Validate benchmark exists
BENCHMARK_PATH="./benchmarks/$BENCHMARK_NAME"
if [ ! -f "$BENCHMARK_PATH" ]; then
    echo "Error: Benchmark '$BENCHMARK_NAME' not found at $BENCHMARK_PATH"
    echo "Available benchmarks in ./benchmarks/:"
    ls -1 ./benchmarks/ 2>/dev/null || echo "  (directory not found)"
    exit 1
fi

# Capture all remaining arguments as benchmark options
BENCHMARK_OPTS="$@"

# Extract number of players from options (default to 2 if not specified)
players=2
for arg in $BENCHMARK_OPTS; do
    if [[ "$prev_arg" == "-n" ]] || [[ "$prev_arg" == "--num-parties" ]]; then
        players="$arg"
    fi
    prev_arg="$arg"
done

# Extract vec_size from options (default to 1000 if not specified)
vec_size=1000
for arg in $BENCHMARK_OPTS; do
    if [[ "$prev_arg" == "-v" ]] || [[ "$prev_arg" == "--vec-size" ]]; then
        vec_size="$arg"
    fi
    prev_arg="$arg"
done

# If -n/--num-parties not found in options, add default
if [[ ! " $BENCHMARK_OPTS " =~ " -n " ]] && [[ ! " $BENCHMARK_OPTS " =~ " --num-parties " ]]; then
    BENCHMARK_OPTS="$BENCHMARK_OPTS -n $players"
fi

# Create results directory structure: Results/<benchmark_name>/<num_parties>
dir=$PWD/../Results/$BENCHMARK_NAME/$players\_PC/$vec_size

# Clean up old results for this benchmark and party configuration
# rm -rf $dir

echo "Running benchmark: $BENCHMARK_NAME"
echo "Number of players: $players"
echo "Number of players: $players"
echo "Benchmark options: $BENCHMARK_OPTS"
echo "Results directory: $dir"
echo ""

for rounds in 1
do
    for party in $(seq 1 $players)
    do
        logdir=$dir
        mkdir -p $logdir
        log=$logdir/party_$party.log
        tplog=$logdir/party_0.log

        # Run benchmark for each party with --localhost option
        eval "$BENCHMARK_PATH $BENCHMARK_OPTS --localhost -p $party" 2>&1 | cat > $log &
        codes[$party]=$!
    done
    
    # Run benchmark for party 0 (trusted party)
    eval "$BENCHMARK_PATH $BENCHMARK_OPTS --localhost -p 0" 2>&1 | cat > $tplog & 
    codes[0]=$!
    
    # Wait for all parties to complete
    for party in $(seq 0 $players)
    do
        wait ${codes[$party]} || return 1
    done
done

echo "Benchmark execution completed. Logs saved to: $logdir"
echo ""

# Run aggregation script if it exists
if [ -f "/code/pythonScripts/getAggStat.py" ]; then
    echo "Running aggregation script..."
    python3 /code/pythonScripts/getAggStat.py $logdir/
fi