#!/bin/bash
# set -x

dir=~/benchmark_data/Darkpool_VM
highestPlayer=$3
startPlayer=4
increment=4

rm -rf $dir/*.log $dir/b*.json
mkdir -p $dir

if test $highestPlayer -lt $startPlayer
then
    echo "Highest player number should not be less than $startPlayer"
else
    for players in $(seq $startPlayer $increment $highestPlayer)
    do
        for party in $(seq 1 $players)
        do
            log=$dir/b_$1_s_$2_$party.log
            json=$dir/b_$1_s_$2_$party.json
            if test $party = 1
            then
                ./benchmarks/Darkpool_VM -p $party --localhost -b $1 -s $2 -n $players -o $json 2>&1 | cat >> $log &
            else
                ./benchmarks/Darkpool_VM -p $party --localhost -b $1 -s $2 -n $players 2>&1 | cat > /dev/null &
            fi
            codes[$i]=$!
        done

        ./benchmarks/Darkpool_VM -p 0 --localhost -b $1 -s $2 -n $players -o $dir/b_$1_s_$2_0.json 2>&1 | tee -a $dir/b_$1_s_$2_0.log

        for party in $(seq 1 $players)
        do
            wait ${codes[$i]} || return 1
        done
    done
fi

