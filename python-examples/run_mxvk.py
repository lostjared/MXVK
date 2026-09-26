#!/usr/bin/env python3

import os
import runpy
import sys
from pathlib import Path

MXVK_DIR = Path(r"C:\acmx\MXVK")
PYTHON_MODULE_DIR = MXVK_DIR / "python_mod"
VCPKG_BIN = Path(r"C:\vcpkg\installed\x64-windows\bin")
CUDA_BIN = Path(r"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4\bin\x64")

def main():
    if(len(sys.argv) < 2):
        print(f"Usage: {Path(sys.argv[0]).name} script.py [arguments...]")
        return 1

    script = Path(sys.argv[1]).resolve()

    if(not script.exists()):
        print(f"Script not found: {script}")
        return 1

    sys.path.insert(0, str(PYTHON_MODULE_DIR))

    dll_dirs = []
    dll_dirs.append(os.add_dll_directory(str(VCPKG_BIN)))
    dll_dirs.append(os.add_dll_directory(str(CUDA_BIN)))

    sys.argv = [str(script)] + sys.argv[2:]
    runpy.run_path(str(script), run_name="__main__")

    return 0

if(__name__ == "__main__"):
    raise SystemExit(main())