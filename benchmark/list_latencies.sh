#!/bin/sh
for bench in $(benchmark/gen_run_script.py -t | awk -F ';' '{print $1}' | tail -n +2); do
    echo "printing seconds for $bench"
    cat result/*_memsec_"$bench"/stats.txt | grep simSeconds
done
