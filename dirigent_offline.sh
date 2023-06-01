#!/bin/bash
# set -x

dir=~/benchmark_data/dirigent_offline
highestPlayer=$3
startPlayer=4
increment=4

rm -rf $dir/*.log $dir/g*.json
mkdir -p $dir

if test $highestPlayer -lt $startPlayer
then
    echo "Highest player number should not be less than $startPlayer"
else
    for players in {5,10,25,50,100}
    # players=$startPlayer
    # while [[ $players -le $highestPlayer ]]
    # for players in $(seq $startPlayer $increment $highestPlayer)
    do
        for party in $(seq 1 $players)
        do
            log=$dir/g_$1_d_$2_$party.log
            json=$dir/g_$1_d_$2_$party.json
            if test $party = 1
            then
                ./benchmarks/dirigent_offline -p $party --localhost -g $1 -d $2 -n $players -o $json 2>&1 | cat >> $log &
            else
                ./benchmarks/dirigent_offline -p $party --localhost -g $1 -d $2 -n $players 2>&1 | cat > /dev/null &
            fi
            codes[$party]=$!
        done

        ./benchmarks/dirigent_offline -p 0 --localhost -g $1 -d $2 -n $players -o $dir/g_$1_d_$2_0.json 2>&1 | tee -a $dir/g_$1_d_$2_0.log &
        codes[0]=$!

        for party in $(seq 0 $players)
        do
            wait ${codes[$party]} || return 1
        done
        # (( players *= $increment ))
    done
fi

