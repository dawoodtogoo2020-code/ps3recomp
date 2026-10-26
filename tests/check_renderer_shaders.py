#!/usr/bin/env python3
"""Compile the actual replacement shaders and validate their SPIR-V (no GPU).

Requires a C compiler, glslangValidator and spirv-val on PATH.
No network, downloads or persistent generated files.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run(args, check=True)


def main():
    with tempfile.TemporaryDirectory(prefix="rsx-sampling-") as directory:
        work = Path(directory)
        binary = work / "contract"
        run(os.environ.get("CC", "cc"), "-std=c17", "-Wall", "-Wextra", "-Werror",
            str(ROOT / "tests/test_sample_transfer_contract.c"), "-o", str(binary))
        run(str(binary))
        for name in ("vertex", "extract", "insert", "shadow"):
            stage = "vert" if name == "vertex" else "frag"
            source = work / (name + ".hlsl")
            source.write_bytes(subprocess.check_output([str(binary), name]))
            spirv = work / (name + ".spv")
            run("glslangValidator", "-D", "-V", "--target-env", "vulkan1.0",
                "--auto-map-bindings", "-S", stage, "-e", "main", "-o", str(spirv), str(source))
            run("spirv-val", "--target-env", "vulkan1.0", str(spirv))
    print("four production shaders compiled and passed SPIR-V validation")


if __name__ == "__main__":
    main()
