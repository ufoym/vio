#!/usr/bin/env python3
"""Print the names of the tests in one shard of a ctest suite.

shard.py <build-dir> <i> <n> [ctest selection args...]

Tests linked by DEPENDS (a -cmp test and the test producing its file, ...)
always land in the same shard.  These groups are spread over the n shards
heaviest first, using rough costs: the long tjunittest runs dominate under
emulation, everything else is short.
"""
import json, re, subprocess, sys

build, i, n, *args = sys.argv[1:]
i, n = int(i), int(n)
tests = json.loads(subprocess.run(["ctest", "--show-only=json-v1", *args], cwd=build,
                                  capture_output=True, text=True, check=True).stdout)["tests"]
names = [t["name"] for t in tests]
index = {name: k for k, name in enumerate(names)}

parent = list(range(len(names)))


def find(k):
    while parent[k] != k:
        parent[k] = parent[parent[k]]
        k = parent[k]
    return k


for k, t in enumerate(tests):
    for prop in t.get("properties", []):
        if prop["name"] == "DEPENDS":
            for dep in prop["value"]:
                if dep in index:
                    parent[find(k)] = find(index[dep])


def cost(name):
    if re.fullmatch(r"tjunittest-simd(-alloc)?", name):
        return 1300
    if re.fullmatch(r"tjunittest(12)?-(simd|scalar)(-alloc|-yuv|-yuv-alloc|-yuv-nopad)?", name):
        return 400
    return 3


groups = {}
for k, name in enumerate(names):
    groups.setdefault(find(k), []).append(name)
load = [0] * n
members = [[] for _ in range(n)]
weighted = ((sum(map(cost, g)), g) for g in groups.values())
for weight, g in sorted(weighted, key=lambda item: (-item[0], item[1][0])):
    s = load.index(min(load))
    load[s] += weight
    members[s] += g
print("\n".join(sorted(members[i - 1], key=index.get)))
