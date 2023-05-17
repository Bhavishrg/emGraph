#!/bin/bash
# set -x

rm -rf *.log g*.json
highestPlayer=$3
startPlayer=4
increment=4

if test $highestPlayer -le $startPlayer
then
    echo "Highest player number should not be less than $startPlayer"
else
    for players in $(seq $startPlayer $increment $highestPlayer)
    do
        for party in $(seq 0 $players)
        do
            log=g_$1_d_$2_$party.log
            json=g_$1_d_$2_$party.json
            if test $party = 0 || test $party = 1
            then
                ./benchmarks/dirigent_mpc -p $party --localhost -g $1 -d $2 -n $players -o $json 2>&1 | cat >> $log &
            else
                ./benchmarks/dirigent_mpc -p $party --localhost -g $1 -d $2 -n $players 2>&1 | cat > /dev/null &
            fi
            codes[$i]=$!
        done
        for party in $(seq 0 $players)
        do
            wait ${codes[$i]} || return 1
        done
    done
fi

