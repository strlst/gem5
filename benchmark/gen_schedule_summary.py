#!/usr/bin/env python3

# integrity_base_address = int('180000000', 16)
hits = {}
binmask = 0xFFFFE0000000

with open("m5out/dramsim3.schedule.txt") as f:
    # with open('result/538.imagick_r_0_1107_1454_memsec_0/dramsim3.schedule.txt', 'r') as f:
    f.readline()
    for line in f:
        binned = hex(int(line.strip().split(";")[3], 16) & binmask)
        hits[binned] = hits.get(binned, 0) + 1

print(hits)
keys = list(hits.keys())
for i, k in enumerate(keys):
    for l in keys[i + 1 :]:
        print(
            f"imbalance({k}, {l}) = min({k}, {l}) / max({k}, {l}) = {min(hits[k], hits[l])} / {max(hits[k], hits[l])} = {round(min(hits[k], hits[l]) * 100 / max(hits[k], hits[l]), 2)}%"
        )
