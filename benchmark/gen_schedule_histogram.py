#!/usr/bin/env python3
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns

# Use seaborn style
sns.set(style="whitegrid")

# with open('m5out/dramsim3.schedule.txt', 'r') as f:
with open(
    "result/538.imagick_r_0_1107_1454_memsec_0/dramsim3.schedule.txt"
) as f:
    data_str = f.read()

# Read into DataFrame
df = pd.read_csv(pd.io.common.StringIO(data_str), sep=";")
df["addr"] = df["hex_address"].apply(lambda x: int(x, 16))

num_bins = 4
counts, bin_edges = np.histogram(df["addr"], bins=num_bins)
bin_left = bin_edges[:-1]
bin_right = bin_edges[1:]
bin_center = (bin_left + bin_right) / 2
mask = counts > 0
counts = counts[mask]
bin_center = bin_center[mask]
widths = bin_right - bin_left
widths = widths[mask]

plt.figure(figsize=(10, 5))
plt.bar(
    bin_center, counts, width=widths, align="center", color="C0", edgecolor="k"
)

# set hex x-tick labels at bin centers
xticks = bin_center
xticklabels = [hex(int(x)) for x in xticks]
plt.xticks(xticks, xticklabels, rotation=45, ha="right")

plt.xlabel("Memory address (hex)")
plt.ylabel("Number of accesses")
plt.title("Memory accesses per address range")
plt.tight_layout()
plt.show()
