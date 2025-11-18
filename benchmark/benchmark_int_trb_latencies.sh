#!/bin/bash
mkdir -p result
(echo "int_trb_size;latency"
for i in $(seq 0 10); do
    echo -n "$((2 ** $i));"
    make OVERRIDE=--int-trb-size=$((2 ** $i)) 2>/dev/null \
        | tail -n 1 \
        | awk '{print $NF}'
done) | tee result/int-trb-sizes-latencies
