#!/usr/bin/env python3
"""Runs the PNG benchmarks over a corpus and summarizes them.

  png_bench.py run --corpus DIR --out RESULTS [--sample N] [--reps R]
               [--vio build/native/png-bench] [--image-png PATH] [--fpnge]
               [--modes decode,encode]
  png_bench.py report RESULTS... [--by-category]

`run` lists the corpus's PNG files (sorted; --sample N keeps a deterministic
1-in-N subset), runs every implementation on them and appends the per-file
lines to RESULTS.  `report` prints, per mode and implementation, the mean
and geometric-mean throughput in megapixels per second over the files that
every implementation of that mode handled, the size of the encoded output
in bits per pixel, and the throughput relative to vio.
"""
import argparse
import collections
import math
import os
import subprocess
import sys
import zlib


def corpus_files(root, sample):
    files = []
    for d, _, names in os.walk(root):
        for n in names:
            if n.lower().endswith(".png"):
                files.append(os.path.join(d, n))
    files.sort()
    if sample > 1:
        files = [f for f in files if zlib.crc32(os.path.relpath(f, root).encode()) % sample == 0]
    return files


def run(args):
    files = corpus_files(args.corpus, args.sample)
    listing = "\n".join(files) + "\n"
    modes = args.modes.split(",")
    jobs = []
    for mode in modes:
        jobs.append([args.vio, "vio", mode, str(args.reps)])
        if args.image_png:
            jobs.append([args.image_png, mode, str(args.reps)])
        if args.fpnge and mode == "encode":
            jobs.append([args.vio, "fpnge", mode, str(args.reps)])
    with open(args.out, "a") as out:
        for cmd in jobs:
            print("running:", " ".join(cmd), f"({len(files)} files)", file=sys.stderr)
            p = subprocess.run(cmd, input=listing, capture_output=True, text=True)
            out.write(p.stdout)
            if p.stderr:
                sys.stderr.write(p.stderr[-2000:])


def load(paths):
    # (mode, impl) -> {file: (mp, seconds, bytes)}
    data = collections.defaultdict(dict)
    for path in paths:
        with open(path) as f:
            for line in f:
                parts = line.split()
                if len(parts) < 6:
                    continue
                impl, mode, mp, sec, nbytes = parts[0], parts[1], *parts[-3:]
                file = " ".join(parts[2:-3])
                data[(mode, impl)][file] = (float(mp), float(sec), int(nbytes))
    return data


def category(file):
    parts = file.replace("\\", "/").split("/")
    return parts[-2] if len(parts) > 1 else ""


def summarize(rows, files):
    speeds = [rows[f][0] / rows[f][1] for f in files if rows[f][1] > 0]
    mp = sum(rows[f][0] for f in files)
    mean = sum(speeds) / len(speeds)
    geo = math.exp(sum(math.log(s) for s in speeds) / len(speeds))
    bpp = sum(rows[f][2] for f in files) * 8 / (mp * 1e6)
    return mean, geo, bpp


def report(args):
    data = load(args.results)
    for mode in sorted({m for m, _ in data}):
        impls = sorted(i for m, i in data if m == mode)
        common = set.intersection(*(set(data[(mode, i)]) for i in impls))
        groups = {"all": sorted(common)}
        if args.by_category:
            for f in sorted(common):
                groups.setdefault(category(f), []).append(f)
        print(f"\n{mode}: {len(common)} files")
        hdr = f"{'set':<22}{'impl':<12}{'mean MP/s':>11}{'geomean':>10}"
        if mode == "encode":
            hdr += f"{'bits/px':>9}"
        hdr += f"{'vio/impl':>10}"
        print(hdr)
        for g, files in groups.items():
            stats = {i: summarize(data[(mode, i)], files) for i in impls}
            for i in impls:
                mean, geo, bpp = stats[i]
                line = f"{g:<22}{i:<12}{mean:>11.1f}{geo:>10.1f}"
                if mode == "encode":
                    line += f"{bpp:>9.3f}"
                if "vio" in stats and i != "vio":
                    line += f"{stats['vio'][1] / geo:>9.2f}x"
                print(line)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--corpus", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--sample", type=int, default=1)
    r.add_argument("--reps", type=int, default=5)
    r.add_argument("--vio", default="build/native/png-bench")
    r.add_argument("--image-png")
    r.add_argument("--fpnge", action="store_true")
    r.add_argument("--modes", default="decode,encode")
    p = sub.add_parser("report")
    p.add_argument("results", nargs="+")
    p.add_argument("--by-category", action="store_true")
    a = ap.parse_args()
    run(a) if a.cmd == "run" else report(a)


if __name__ == "__main__":
    main()
