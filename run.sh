#!/bin/bash
# Runs a predefined set of benchmarks and stores logs under Results/
# Mirrors style and logging conventions from graph_analysis.sh

set -e

ROOT_DIR=$(pwd)
BENCH_DIR=$ROOT_DIR/benchmarks
RESULTS_DIR=$PWD/../Results

mkdir -p "$RESULTS_DIR"

run_benchmark() {
    local bench_exe="$1"
    shift
    local opts="$@"

    if [ ! -f "$bench_exe" ]; then
        echo "Benchmark executable not found: $bench_exe"
        return 1
    fi

    # Extract num parties and vec-size from opts
    players=2
    vec_size=1000
    prev_arg=""
    for arg in $opts; do
        if [[ "$prev_arg" == "-n" ]] || [[ "$prev_arg" == "--num-parties" ]]; then
            players="$arg"
        fi
        if [[ "$prev_arg" == "-v" ]] || [[ "$prev_arg" == "--vec-size" ]]; then
            vec_size="$arg"
        fi
        prev_arg="$arg"
    done

    # Ensure options contain -n and -v
    if [[ ! " $opts " =~ " -n " ]] && [[ ! " $opts " =~ " --num-parties " ]]; then
        opts="$opts -n $players"
    fi
    if [[ ! " $opts " =~ " -v " ]] && [[ ! " $opts " =~ " --vec-size " ]]; then
        opts="$opts -v $vec_size"
    fi

    dir="$RESULTS_DIR/$(basename $bench_exe)/${players}_PC/$vec_size"
    mkdir -p "$dir"

    echo "Running $(basename $bench_exe) with players=$players vec_size=$vec_size"

    for party in $(seq 1 $players); do
        log=$dir/party_${party}.log
        echo "  starting party $party -> $log"
        "$bench_exe" $opts --localhost -p $party 2>&1 | cat > "$log" &
        pids[$party]=$!
    done

    # trusted party 0
    tplog=$dir/party_0.log
    "$bench_exe" $opts --localhost -p 0 2>&1 | cat > "$tplog" &
    pids[0]=$!

    # wait
    for party in $(seq 0 $players); do
        wait ${pids[$party]}
    done

    echo "Completed $(basename $bench_exe) players=$players vec_size=$vec_size -> logs: $dir"

    if [ -f "/code/pythonScripts/getAggStat.py" ]; then
        echo "Running aggregation script..."
        python3 /code/pythonScripts/getAggStat.py $dir/
    fi
}

##########################
# Define parameter sets
##########################

COMMON_VEC=100000
PARTIES="2 5 10 15 20 25"

echo "Starting batch run at $(date)"

# 1. mpa_emgraph for vec-size 100000, num-parties = 2,5,10,15,20,25
for p in $PARTIES; do
    run_benchmark "$BENCH_DIR/mpa_grasp" "--vec-size $COMMON_VEC -n $p"
done

# # 2. mpa_graphiti for vec-size 100000, same parties
for p in $PARTIES; do
    run_benchmark "$BENCH_DIR/mpa_graphiti" "--vec-size $COMMON_VEC -n $p"
done

# # 3. e2e_emgraph for vec-size 100000 and num parties=2,5,10,15,25
for p in $PARTIES; do
    run_benchmark "$BENCH_DIR/e2e_grasp" "--vec-size $COMMON_VEC -n $p -i 10"
done

# # 4. e2e_emgraph for num parties =5 and vec-size= 10000,100000,1000000,10000000
for v in 10000 100000 1000000 10000000; do
    run_benchmark "$BENCH_DIR/e2e_grasp" "--vec-size $v -n 5 -i 10"
done

# 5. e2e_graphiti for vec-size 100000 and num parties= 2,5,10,15
for p in 2 5 10 15; do
    run_benchmark "$BENCH_DIR/e2e_graphiti" "--vec-size $COMMON_VEC -n $p -i 10"
done

# # 6. e2e_graphiti for num parties=5 and vec-size=10000,100000
for v in 10000; do
    run_benchmark "$BENCH_DIR/e2e_graphiti" "--vec-size $v -n 5 -i 10"
done

echo "Batch run finished"

echo "Running table generation script..."
# python3 /code/pythonScripts/generate_tables.py $RESULTS_DIR/
