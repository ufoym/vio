#!/usr/bin/env python3
"""Combine CI results into one report.

report.py <results-dir> <report.md> <report.html>

<results-dir> holds the files written by run-ctest.sh (*.json + JUnit *.xml)
and bench.py (bench-*.json), in any subdirectory layout.
"""
import glob, html, json, math, os, sys, xml.etree.ElementTree as ET

root, md_path, html_path = sys.argv[1:4]
meta = {"sha": os.environ.get("GITHUB_SHA", "")[:10],
        "ref": os.environ.get("GITHUB_REF_NAME", ""),
        "run": (f"{os.environ.get('GITHUB_SERVER_URL')}/"
                f"{os.environ.get('GITHUB_REPOSITORY')}/actions/runs/"
                f"{os.environ.get('GITHUB_RUN_ID')}"
                if os.environ.get("GITHUB_RUN_ID") else "")}

# ---- functional tests -------------------------------------------------------
tests = []
for path in sorted(glob.glob(f"{root}/**/*.json", recursive=True)):
    if os.path.basename(path).startswith("bench-"):
        continue
    info = json.load(open(path))
    xml = path[:-5] + ".xml"
    total = failed = skipped = 0
    names = []
    if os.path.exists(xml):
        for case in ET.parse(xml).getroot().iter("testcase"):
            total += 1
            if case.get("status") in ("disabled", "notrun") or \
                    case.find("skipped") is not None:
                skipped += 1
            elif case.find("failure") is not None or \
                    case.get("status") == "fail":
                failed += 1
                names.append(case.get("name"))
    ok = info["exit"] == 0 and failed == 0 and total > 0
    tests.append({**info, "total": total, "failed": failed,
                  "skipped": skipped, "names": names, "ok": ok})

# ---- performance ------------------------------------------------------------
bench = [json.load(open(p)) for p in
         sorted(glob.glob(f"{root}/**/bench-*.json", recursive=True))]


def geomean(xs):
    return math.exp(sum(map(math.log, xs)) / len(xs)) if xs else float("nan")


def ratio(r, op):
    return r[op]["vio"] / r[op]["ref"]


for b in bench:
    for isa in b["isas"]:
        isa["gc"] = geomean([ratio(r, "compress") for r in isa["rows"]])
        isa["gd"] = geomean([ratio(r, "decompress") for r in isa["rows"]])

# ---- Markdown ---------------------------------------------------------------
def pct(x):
    return f"{100 * x:.1f}%"


def flag(x):  # vio relative to libjpeg-turbo
    return "🟢" if x >= 0.97 else "🟡" if x >= 0.90 else "🔴"


md = ["# vio CI report", ""]
if meta["sha"]:
    md.append(f"Commit `{meta['sha']}` on `{meta['ref']}` · [workflow run]({meta['run']})")
    md.append("")
passed_runs = sum(t["ok"] for t in tests)
md += [f"## Functional tests — {passed_runs}/{len(tests)} runs passed", "",
       "| Platform | Run | Result | Passed | Time | Environment |",
       "| --- | --- | --- | --- | --- | --- |"]
for t in tests:
    res = "✅" if t["ok"] else "❌"
    run = t["total"] - t["skipped"]
    args = t["ctest_args"].replace("|", "\\|")
    md.append(f"| {t['platform']} | {t['run']} | {res} | "
              f"{run - t['failed']}/{run} | {t['seconds']}s | "
              f"`{t['env'] or '-'}` {args} |")
failures = [t for t in tests if t["names"]]
if failures:
    md += ["", "<details><summary>Failed tests</summary>", ""]
    for t in failures:
        md.append(f"- **{t['platform']} / {t['run']}**: "
                  + ", ".join(f"`{n}`" for n in t["names"][:50])
                  + (" …" if len(t["names"]) > 50 else ""))
    md += ["", "</details>"]

md += ["", "## Performance — vio throughput relative to libjpeg-turbo", "",
       "Geometric mean over all configurations; 100% = same speed. "
       "Measured on GitHub-hosted runners (shared VMs: expect ±5% noise).", "",
       "| Platform | CPU | ISA | Compress | Decompress |",
       "| --- | --- | --- | --- | --- |"]
for b in bench:
    for isa in b["isas"]:
        md.append(f"| {b['platform']} | {b['cpu']} | {isa['isa']} | "
                  f"{flag(isa['gc'])} {pct(isa['gc'])} | "
                  f"{flag(isa['gd'])} {pct(isa['gd'])} |")
for b in bench:
    for isa in b["isas"]:
        md += ["", f"<details><summary>{b['platform']} · {isa['isa']} "
               f"(Mpixels/s, {b['image']} image)</summary>", "",
               "| Config | Compress vio | ref | % | Decompress vio | ref | % |",
               "| --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
        for r in isa["rows"]:
            c, d = r["compress"], r["decompress"]
            md.append(f"| {r['config']} | {c['vio']:.1f} | {c['ref']:.1f} | "
                      f"{pct(ratio(r, 'compress'))} | {d['vio']:.1f} | "
                      f"{d['ref']:.1f} | {pct(ratio(r, 'decompress'))} |")
        md += ["", "</details>"]
open(md_path, "w").write("\n".join(md) + "\n")

# ---- HTML -------------------------------------------------------------------
e = html.escape


def cell(x):
    cls = "good" if x >= 0.97 else "warn" if x >= 0.90 else "bad"
    return f'<td class="num {cls}">{pct(x)}</td>'


h = []
h.append("<h2>Functional tests <small>"
         f"{passed_runs}/{len(tests)} runs passed</small></h2>")
h.append("<table><tr><th>Platform</th><th>Run</th><th>Result</th>"
         "<th>Passed</th><th>Time</th><th>Environment</th></tr>")
for t in tests:
    run = t["total"] - t["skipped"]
    h.append(f"<tr><td>{e(t['platform'])}</td><td>{e(t['run'])}</td>"
             f"<td class=\"{'good' if t['ok'] else 'bad'}\">"
             f"{'pass' if t['ok'] else 'FAIL'}</td>"
             f"<td class=num>{run - t['failed']}/{run}</td>"
             f"<td class=num>{t['seconds']}s</td>"
             f"<td><code>{e(t['env'] or '-')}</code> {e(t['ctest_args'])}</td></tr>")
    if t["names"]:
        h.append(f"<tr><td></td><td colspan=5 class=fail>"
                 f"{e(', '.join(t['names']))}</td></tr>")
h.append("</table>")

h.append("<h2>Performance <small>vio ÷ libjpeg-turbo throughput, "
         "geometric mean</small></h2>")
h.append("<table><tr><th>Platform</th><th>CPU</th><th>ISA</th>"
         "<th>Compress</th><th>Decompress</th></tr>")
for b in bench:
    for isa in b["isas"]:
        h.append(f"<tr><td>{e(b['platform'])}</td><td>{e(b['cpu'])}</td>"
                 f"<td>{e(isa['isa'])}</td>{cell(isa['gc'])}{cell(isa['gd'])}</tr>")
h.append("</table>")
for b in bench:
    for isa in b["isas"]:
        h.append(f"<h3>{e(b['platform'])} · {e(isa['isa'])} "
                 f"<small>Mpixels/s, {e(b['image'])} image, median of "
                 f"{b['reps']}</small></h3>")
        h.append("<table><tr><th>Config</th><th>Compress vio</th><th>ref</th>"
                 "<th>%</th><th>Decompress vio</th><th>ref</th><th>%</th></tr>")
        for r in isa["rows"]:
            c, d = r["compress"], r["decompress"]
            h.append(f"<tr><td>{e(r['config'])}</td>"
                     f"<td class=num>{c['vio']:.1f}</td><td class=num>{c['ref']:.1f}</td>"
                     f"{cell(ratio(r, 'compress'))}"
                     f"<td class=num>{d['vio']:.1f}</td><td class=num>{d['ref']:.1f}</td>"
                     f"{cell(ratio(r, 'decompress'))}</tr>")
        h.append("</table>")

header = (f"Commit <code>{e(meta['sha'])}</code> on <code>{e(meta['ref'])}</code>"
          f" · <a href=\"{e(meta['run'])}\">workflow run</a>"
          if meta["sha"] else "")
page = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>vio CI report</title>
<style>
:root {{ --bg:#fff; --fg:#1d1f23; --muted:#667; --line:#dde1e6; --head:#f4f6f8;
        --good:#1a7f37; --warn:#9a6700; --bad:#cf222e; }}
@media (prefers-color-scheme: dark) {{
  :root {{ --bg:#0d1117; --fg:#e6edf3; --muted:#8b949e; --line:#30363d; --head:#161b22;
          --good:#3fb950; --warn:#d29922; --bad:#f85149; }} }}
body {{ background:var(--bg); color:var(--fg); margin:0 auto; max-width:1100px;
       padding:24px 16px; font:14px/1.5 system-ui, sans-serif; }}
h1 {{ margin:0 0 4px; }} h2 {{ margin-top:32px; }} small {{ color:var(--muted); font-weight:400; }}
table {{ border-collapse:collapse; width:100%; margin:8px 0 16px; display:block; overflow-x:auto; }}
th, td {{ border-bottom:1px solid var(--line); padding:4px 10px; text-align:left; white-space:nowrap; }}
th {{ background:var(--head); }}
.num {{ text-align:right; font-variant-numeric:tabular-nums; }}
.good {{ color:var(--good); }} .warn {{ color:var(--warn); }} .bad {{ color:var(--bad); font-weight:600; }}
.fail {{ color:var(--bad); white-space:normal; font-family:ui-monospace, monospace; font-size:12px; }}
</style></head><body>
<h1>vio CI report</h1><div>{header}</div>
{''.join(h)}
</body></html>
"""
open(html_path, "w").write(page)
