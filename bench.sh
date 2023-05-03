#!/usr/bin/env bash
prog=$1
args=${@:2}
n=30
for (( i=0; i<=$n; i++ ))
do
    osascript -e "tell application \"Terminal\" to do script \"cd Desktop/TP_Aided_MPC/dirigentv2; 
    $prog -p $i --localhost $args -n $n; $SHELL\""
done