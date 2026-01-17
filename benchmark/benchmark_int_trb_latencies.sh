#!/bin/bash
mkdir -p result
(echo "int_trb_size;latency"
for i in $(seq 0 6); do
    echo -n "$((2 ** $i));"
    make --no-print-directory OVERRIDE="--no-int-merge-req --no-int-defrag-req --int-parallel-dispatch --no-int-similar-dispatch --metadata-cache-size=32KiB --int-trb-size="$((2 ** $i)) 2>/dev/null \
        | tail -n 1 \
        | awk '{print $NF}'
done) | tee result/int-trb-sizes-latencies
