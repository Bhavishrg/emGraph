#!/bin/bash

## Asterisk

players=$3
# echo $(($players-1))
for party in $(seq 0 $players)
do
    log=opt_$party.log
    ./benchmarks/dirigent_mpc -p $party --localhost -g $1 -d $2 -n $players 2>&1 |
    # ./mascot-party.x $party -F -N $1 $2 -v 2>&1 | 
    {
        if test $party = 0; then tee $log; else cat > $log; fi;
    } &
    codes[$i]=$!
    # echo $party
done
for party in $(seq 0 $players)
do
    wait ${codes[$i]} || return 1
done

## Quadsquad

#players=3
## echo $(($players-1))
#for party in $(seq 0 $players)
#do
#    log=opt_$party.log
#    ./benchmarks/online_mpc_new -p $party --localhost -g $1 -d $2 2>&1 |
#    # ./mascot-party.x $party -F -N $1 $2 -v 2>&1 | 
#    {
#        if test $party = 0; then tee $log; else cat > $log; fi;
#    } &
#    codes[$i]=$!
#    # echo $party
#done
#for party in $(seq 0 $players)
#do
#    wait ${codes[$i]} || return 1
#done

