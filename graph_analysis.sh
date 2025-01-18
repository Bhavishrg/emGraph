#!/bin/bash
# set -x

dir=$PWD/../Results

# rm -rf $dir/*.log $dir/g*.json
rm -rf $dir
mkdir -p $dir
vec_size="10000"

for players in {2,5,10,15,20,25}
# for players in 10
do
    # for vec_size in {10000,100000,1000000,10000000}
    # do
        # for rounds in {1,2,3}
        for rounds in 1
        do
            for party in $(seq 1 $players)
            do
                logdir=$dir/$players\_PC/$vec_size\_Nodes/TestRun/
                mkdir -p $logdir
                log=$logdir/round\_$rounds\_party\_$party.log
                tplog=$logdir/round\_$rounds\_party\_0.log
                if [ $party -eq 1 ]; then
                    # gdb --args
                    # valgrind --leak-check=full -v
                    # initialization_emgraph
                    # test_primitives
                    # mpa_emgraph
                    # mpa_graphiti
                    ./benchmarks/test_primitives -p $party --localhost -v $vec_size -n $players 2>&1 | cat > $log &
                else
                    ./benchmarks/test_primitives -p $party --localhost -v $vec_size -n $players 2>&1 | cat > $log &
                fi
                codes[$party]=$!
            done

            ./benchmarks/test_primitives -p 0 --localhost -v $vec_size -n $players 2>&1 | cat > $tplog &
            codes[0]=$!
            for party in $(seq 0 $players)
            do
                wait ${codes[$party]} || return 1
            done
        done
    # done
done
