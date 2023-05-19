#!/bin/bash
# set -x

dir=~/benchmark_data/auction
highestPlayer=$2
startPlayer=4
increment=4

rm -rf $dir/*.log $dir/b*.json
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
            log=$dir/b_$1_$party.log
            json=$dir/b_$1_$party.json
            if test $party = 1
            then
                ./benchmarks/auction -p $party --localhost -b $1 -n $players -o $json 2>&1 | cat >> $log &
            else
                ./benchmarks/auction -p $party --localhost -b $1 -n $players 2>&1 | cat > /dev/null &
            fi
            codes[$i]=$!
        done

        ./benchmarks/auction -p 0 --localhost -b $1 -n $players -o $dir/b_$1_0.json 2>&1 | tee -a $dir/b_$1_0.log 

        for party in $(seq 0 $players)
        do
            wait ${codes[$i]} || return 1
        done
        # (( players *= $increment ))
    done
fi

