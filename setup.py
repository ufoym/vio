# Builds the vio._vio extension (bind/python/vio/_vio.cpp, which compiles the
# header-only C interface bind/c/vio.h) as C++20.  Package metadata is in
# pyproject.toml.
from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


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
            sources=["bind/python/vio/_vio.cpp"],
            include_dirs=["include", "bind/c"],
            depends=["bind/c/vio.h"],
            language="c++",
        )
    ],
    cmdclass={"build_ext": BuildExt},
)
