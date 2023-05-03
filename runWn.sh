#!/usr/bin/env bash

# Usage: ./script_name.sh program_name arguments n

prog=$1
args=${@:2:$#-1}
n=$#

# Loop over values of i from 0 to n-1
for (( i=0; i<$n; i++ ))
do
    # Run program with -p i --localhost arguments
    ($prog -p $i --localhost $args -n $n) >/dev/null &
done

# Run program with -p n-1 --localhost argument
($prog -p $(($n)) --localhost $args -n $n)
