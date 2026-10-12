#!/usr/bin/env python3
"""Throughput of vio's tjbench against libjpeg-turbo's on the same machine.

bench.py --vio build/tjbench --ref build/ljt/tjbench --platform linux-x64 \
         --isa avx2= --isa sse2=JSIMD_FORCESSE2=1 --out results/bench-linux-x64.json

Each --isa is LABEL=[VAR=value,...]; both libraries honour the same JSIMD_*
variables, so every row compares like with like.  The two programs are run
alternately, several times, and the best throughput of each is kept:
interference from other jobs on a shared runner can only slow a run down,
so the best of several runs is far more stable than the median.

When a sampling profiler is available (perf on Linux, sample on macOS), the
compression of PROFILE_CONFIG is also profiled per ISA, so that the report
shows where the time goes in both libraries.
"""
import argparse, json, os, platform, re, shutil, subprocess, sys
import tempfile, time

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

PROFILE_CONFIG = ("4:2:0 q95", ["95", "-subsamp", "420"])


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


def short_name(sym):
    """Readable function name: no arguments, template arguments or library."""
    sym = re.sub(r"\s+\(in [^)]*\)\s*$", "", sym.strip())
    for open_, close in (("<", ">"), ("(", ")")):
        out, depth = [], 0
        for ch in sym:
            if ch == open_:
                depth += 1
            elif ch == close and depth:
                depth -= 1
            elif not depth:
                out.append(ch)
        sym = "".join(out)
    sym = sym.replace("vio::jpeg::", "").replace("detail::", "")
    return re.sub(r"\s*\[clone[^]]*\]", "", sym).strip() or "?"


def profiler():
    if sys.platform == "darwin":
        return "sample" if shutil.which("sample") else None
    if shutil.which("perf"):
        ok = subprocess.run(["perf", "stat", "-e", "cpu-clock", "true"],
                            capture_output=True).returncode == 0
        return "perf" if ok else None
    return None


def profile(tool, exe, image, args, env, tmp, annotate=None):
    """[(percent, function)] of one compression run, and its throughput.
    With perf, the instruction-level profile of the two hottest functions is
    also written to the file `annotate`."""
    cmd = [exe, image, *args, "-componly", "-benchtime", "3", "-warmup", "0.5",
           "-nowrite"]
    counts = {}
    if tool == "perf":
        data = os.path.join(tmp, "perf.data")
        out = subprocess.run(["perf", "record", "-q", "-e", "cpu-clock",
                              "-F", "1999", "-o", data, "--", *cmd], env=env,
                             capture_output=True, text=True, timeout=600)
        rep = subprocess.run(["perf", "report", "-i", data, "--stdio", "-q",
                              "--no-children", "--sort", "sym"],
                             capture_output=True, text=True, timeout=600).stdout
        for line in rep.splitlines():
            m = re.match(r"\s*([\d.]+)%\s+\[.\]\s+(.+)", line)
            if m:
                name = short_name(m[2])
                counts[name] = counts.get(name, 0) + float(m[1])
        if annotate:
            raw = subprocess.run(["perf", "report", "-i", data, "--stdio", "-q",
                                  "--no-children", "--sort", "sym", "--no-demangle"],
                                 capture_output=True, text=True, timeout=600).stdout
            syms = [m[1] for m in (re.match(r"\s*[\d.]+%\s+\[.\]\s+(\S+)", l)
                                   for l in raw.splitlines()) if m][:2]
            with open(annotate, "w") as f:
                for sym in syms:
                    ann = subprocess.run(["perf", "annotate", "-i", data, "--stdio",
                                          "--no-source", "--symbol", sym],
                                         capture_output=True, text=True, timeout=600)
                    f.write(f"===== {sym}\n{ann.stdout}\n")
    else:
        report = os.path.join(tmp, "sample.txt")
        p = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        subprocess.run(["sample", str(p.pid), "2", "-f", report],
                       capture_output=True, timeout=120)
        out = subprocess.CompletedProcess(cmd, p.wait(), p.stdout.read(), "")
        section = False
        for line in open(report, errors="replace") if os.path.exists(report) else []:
            if "Sort by top of stack" in line:
                section = True
            elif section and not line.strip():
                break
            elif section:
                m = re.match(r"\s*(.+?)\s+(\d+)\s*$", line)
                if m:
                    name = short_name(m[1])
                    counts[name] = counts.get(name, 0) + int(m[2])
        total = sum(counts.values()) or 1
        counts = {k: 100 * v / total for k, v in counts.items()}
    tput = re.findall(r"Throughput:\s+([\d.]+)", out.stdout)
    rows = sorted(((round(v, 2), k) for k, v in counts.items() if v >= 0.5),
                  reverse=True)
    return {"mpx": float(tput[0]) if tput else None, "rows": rows[:15]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vio", required=True)
    ap.add_argument("--ref", required=True)
    ap.add_argument("--platform", required=True)
    ap.add_argument("--isa", action="append", required=True)
    ap.add_argument("--src", default="tests/libjpeg-turbo/testimages/testorig.ppm")
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--benchtime", type=float, default=1.0)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    tmp = tempfile.mkdtemp()
    image = os.path.join(tmp, "bench.ppm")
    w, h = make_image(a.src, image)
    result = {"platform": a.platform, "cpu": cpu_name(),
              "image": f"{w}x{h}", "reps": a.reps, "isas": []}
    tool = profiler()
    print(f"profiler: {tool or 'none'}", flush=True)

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
            best = {who: [max(s[i] for s in v) for i in (0, 1)]
                   for who, v in samples.items()}
            rows.append({"config": name, "args": " ".join(args),
                         "compress": {"vio": best["vio"][0], "ref": best["ref"][0]},
                         "decompress": {"vio": best["vio"][1], "ref": best["ref"][1]}})
            c, d = rows[-1]["compress"], rows[-1]["decompress"]
            print(f"{a.platform:14} {label:6} {name:20} "
                  f"C {c['vio']:8.1f}/{c['ref']:8.1f} ({100 * c['vio'] / c['ref']:5.1f}%)  "
                  f"D {d['vio']:8.1f}/{d['ref']:8.1f} ({100 * d['vio'] / d['ref']:5.1f}%)",
                  flush=True)
        entry = {"isa": label, "env": vars_, "rows": rows}
        if tool:
            try:
                outdir = os.path.dirname(a.out) or "."
                entry["profile"] = {
                    "tool": tool, "config": PROFILE_CONFIG[0],
                    **{who: profile(tool, exe, image, PROFILE_CONFIG[1], env, tmp,
                                    os.path.join(outdir, f"annotate-{a.platform}-{label}-{who}.txt"))
                       for who, exe in (("vio", a.vio), ("ref", a.ref))}}
            except (OSError, subprocess.SubprocessError) as e:
                print(f"profiling failed: {e}", flush=True)
        result["isas"].append(entry)

    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    json.dump(result, open(a.out, "w"), indent=1)


if __name__ == "__main__":
    main()
