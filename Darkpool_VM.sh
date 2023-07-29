#!/bin/bash
# set -x

dir=~/benchmark_data/Darkpool_VM
highestPlayer=100
startPlayer=4
increment=4

# rm -rf $dir/*.log $dir/b*.json
mkdir -p $dir

if test $highestPlayer -lt $startPlayer
then
    echo "Highest player number should not be less than $startPlayer"
else
    for var in 10
    # for var in {5,10,25,50,100}
    # var=$startPlayer
    # while [[ $var -le $highestPlayer ]]
    # for var in $(seq $startPlayer $increment $highestPlayer)
    do
        players=$(( $var + $var ))
        for party in $(seq 1 $players)
        do
            log=$dir/b_"$var"_s_"$var"_"$party".log
            json=$dir/b_"$var"_s_"$var"_"$party".json
            if [ $party = 1 ] || [ $party -eq $players ]
            then
                ./benchmarks/Darkpool_VM -p $party --localhost -b $var -s $var -n $players -o $json 2>&1 | cat >> /dev/null &
            else
                ./benchmarks/Darkpool_VM -p $party --localhost -b $var -s $var -n $players 2>&1 | cat > /dev/null &
            fi
            codes[$party]=$!
        done

        ./benchmarks/Darkpool_VM -p 0 --localhost -b $var -s $var -n $players -o $dir/b_"$var"_s_"$var"_0.json 2>&1 | tee -a /dev/null &
        codes[0]=$!

        for party in $(seq 0 $players)
        do
            wait ${codes[$party]} || return 1
        done
        # (( players *= $increment ))
    done
fi

