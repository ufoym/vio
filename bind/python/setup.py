# Builds the vio._vio extension and bundles its headers in source distributions.
from pathlib import Path
from shutil import copy2, copytree

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext
from setuptools.command.sdist import sdist


ROOT = Path(__file__).resolve().parent
HEADERS = ROOT if (ROOT / "include").is_dir() else ROOT / "../.."


class Sdist(sdist):
    def make_release_tree(self, base_dir, files):
        super().make_release_tree(base_dir, files)
        copytree(HEADERS / "include", Path(base_dir) / "include", dirs_exist_ok=True)
        target = Path(base_dir) / "bind/c/vio"
        target.mkdir(parents=True, exist_ok=True)
        copy2(HEADERS / "bind/c/vio/vio.h", target / "vio.h")


class BuildExt(build_ext):
    def build_extensions(self):
        if self.compiler.compiler_type == "msvc":
            args = ["/std:c++20", "/O2", "/utf-8", "/EHsc", "/D_CRT_SECURE_NO_WARNINGS"]
        else:
            args = ["-std=c++20", "-O2", "-fvisibility=hidden"]
        for ext in self.extensions:
            ext.extra_compile_args = args
        super().build_extensions()


setup(
    ext_modules=[
        Extension(
            "vio._vio",
            sources=["vio/_vio.cpp"],
            include_dirs=[str(HEADERS / "include"), str(HEADERS / "bind/c/vio")],
            depends=[str(HEADERS / "bind/c/vio/vio.h")],
            language="c++",
        )
    ],
    cmdclass={"build_ext": BuildExt, "sdist": Sdist},
)
