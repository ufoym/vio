#!/usr/bin/env python3
"""Throughput of vio's tjbench against libjpeg-turbo's on the same machine.

bench.py --vio build/tjbench --ref build/ljt/tjbench --platform linux-x64 \
         --isa avx2= --isa sse2=JSIMD_FORCESSE2=1 --out results/bench-linux-x64.json

Each --isa is LABEL=[VAR=value,...]; both libraries honour the same JSIMD_*
variables, so every row compares like with like.  The two programs are run
alternately, several times, and the median throughput is kept, which keeps
shared-runner noise from favouring either side.
"""
import argparse, json, os, platform, re, statistics, subprocess, sys, tempfile

CONFIGS = [
    ("4:4:4 q95", ["95", "-subsamp", "444"]),
    ("4:2:2 q95", ["95", "-subsamp", "422"]),
    ("4:2:0 q95", ["95", "-subsamp", "420"]),
    ("gray q95", ["95", "-subsamp", "gray"]),
    ("4:2:0 fast DCT", ["95", "-subsamp", "420", "-fastdct"]),
    ("4:2:0 progressive", ["95", "-subsamp", "420", "-progressive"]),
    ("4:2:0 arithmetic", ["95", "-subsamp", "420", "-arithmetic"]),
    ("lossless PSV1", ["1", "-lossless"]),
]


def make_image(src, path, tiles=16):
    """Tile a small PPM into a large one (keeps natural-image statistics)."""
    data = open(src, "rb").read()
    fields = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    w, h = int(fields[1]), int(fields[2])
    pix = data[fields.end():fields.end() + w * h * 3]
    rows = [pix[y * w * 3:(y + 1) * w * 3] * tiles for y in range(h)]
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w * tiles, h * tiles))
        for _ in range(tiles):
            f.writelines(rows)
    return w * tiles, h * tiles


def tjbench(exe, image, args, env, benchtime):
    cmd = [exe, image, *args, "-benchtime", str(benchtime), "-warmup", "0.5",
           "-nowrite"]
    out = subprocess.run(cmd, env=env, capture_output=True, text=True,
                         timeout=600)
    tput = [float(m) for m in re.findall(r"Throughput:\s+([\d.]+)", out.stdout)]
    if out.returncode or len(tput) < 2:
        raise RuntimeError(f"{' '.join(cmd)} failed:\n{out.stdout}{out.stderr}")
    return tput[0], tput[1]


def cpu_name():
    try:
        if sys.platform == "darwin":
            return subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"],
                                  capture_output=True, text=True).stdout.strip()
        for line in open("/proc/cpuinfo"):
            if line.startswith(("model name", "Model")):
                return line.split(":", 1)[1].strip()
        out = subprocess.run(["lscpu"], capture_output=True, text=True).stdout
        m = re.search(r"Model name:\s*(.+)", out)
        return m[1].strip() if m else platform.processor()
    except OSError:
        return platform.processor()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vio", required=True)
    ap.add_argument("--ref", required=True)
    ap.add_argument("--platform", required=True)
    ap.add_argument("--isa", action="append", required=True)
    ap.add_argument("--src", default="tests/libjpeg-turbo/testimages/testorig.ppm")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--benchtime", type=float, default=1.0)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    tmp = tempfile.mkdtemp()
    image = os.path.join(tmp, "bench.ppm")
    w, h = make_image(a.src, image)
    result = {"platform": a.platform, "cpu": cpu_name(),
              "image": f"{w}x{h}", "reps": a.reps, "isas": []}

    for spec in a.isa:
        label, _, vars_ = spec.partition("=")
        env = dict(os.environ)
        env.update(v.split("=", 1) for v in vars_.split(",") if v)
        rows = []
        for name, args in CONFIGS:
            samples = {"vio": [], "ref": []}
            for _ in range(a.reps):
                for who, exe in (("ref", a.ref), ("vio", a.vio)):
                    samples[who].append(tjbench(exe, image, args, env,
                                                a.benchtime))
            med = {who: [statistics.median(s[i] for s in v) for i in (0, 1)]
                   for who, v in samples.items()}
            rows.append({"config": name, "args": " ".join(args),
                         "compress": {"vio": med["vio"][0], "ref": med["ref"][0]},
                         "decompress": {"vio": med["vio"][1], "ref": med["ref"][1]}})
            c, d = rows[-1]["compress"], rows[-1]["decompress"]
            print(f"{a.platform:14} {label:6} {name:20} "
                  f"C {c['vio']:8.1f}/{c['ref']:8.1f} ({100 * c['vio'] / c['ref']:5.1f}%)  "
                  f"D {d['vio']:8.1f}/{d['ref']:8.1f} ({100 * d['vio'] / d['ref']:5.1f}%)",
                  flush=True)
        result["isas"].append({"isa": label, "env": vars_, "rows": rows})

    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    json.dump(result, open(a.out, "w"), indent=1)


if __name__ == "__main__":
    main()
