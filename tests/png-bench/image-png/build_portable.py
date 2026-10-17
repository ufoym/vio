#!/usr/bin/env python3
"""Builds the image-png benchmark without its run-time SIMD, for the rows
that compare vio without SIMD (JSIMD_FORCENONE).

image-png's checksums come from crates that pick SIMD code at run time
(crc32fast: PCLMULQDQ or the Arm CRC32 instructions; simd-adler32: SSE2 to
AVX-512 or NEON).  This copies the two crates from cargo's cache into
target/portable-deps, makes them use their portable code, and builds with
those copies patched in, into target/portable/release.  Code that rustc
vectorizes by itself stays, as does the C++ compilers' in vio's scalar code.

  build_portable.py      (run from anywhere; needs cargo)
"""
import json
import os
import shutil
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
manifest = os.path.join(here, "Cargo.toml")
target = os.path.join(here, "target")
deps = os.path.join(target, "portable-deps")

meta = json.loads(subprocess.run(["cargo", "metadata", "--format-version", "1", "--manifest-path", manifest],
                                 check=True, capture_output=True, text=True).stdout)
src = {p["name"]: os.path.dirname(p["manifest_path"]) for p in meta["packages"]}


def patch(name, path, old, new):
    dst = os.path.join(deps, name)
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src[name], dst)
    f = os.path.join(dst, path)
    text = open(f, encoding="utf-8").read()
    if old not in text:
        sys.exit(f"build_portable.py: {name}/{path} has changed; update the patch")
    open(f, "w", encoding="utf-8").write(text.replace(old, new))
    return dst


crc = patch("crc32fast", os.path.join("src", "lib.rs"),
            "        Self::internal_new_specialized(init, amount)\n"
            "            .unwrap_or_else(|| Self::internal_new_baseline(init, amount))",
            "        Self::internal_new_baseline(init, amount)")
adler = patch("simd-adler32", os.path.join("src", "imp", "mod.rs"),
              "  avx512::get_imp()\n"
              "    .or_else(neon::get_imp)\n"
              "    .or_else(avx2::get_imp)\n"
              "    .or_else(ssse3::get_imp)\n"
              "    .or_else(sse2::get_imp)\n"
              "    .or_else(wasm::get_imp)\n"
              "    .unwrap_or(scalar::update)",
              "  scalar::update")


def toml_path(p):
    return p.replace("\\", "/")


# (the patches rewrite Cargo.lock's entries for the two crates: restored after)
lock = os.path.join(here, "Cargo.lock")
saved = open(lock, "rb").read()
try:
    subprocess.run(["cargo", "build", "--release", "--manifest-path", manifest,
                    "--target-dir", os.path.join(target, "portable"),
                    "--config", f'patch.crates-io.crc32fast.path="{toml_path(crc)}"',
                    "--config", f'patch.crates-io.simd-adler32.path="{toml_path(adler)}"'], check=True)
finally:
    open(lock, "wb").write(saved)
