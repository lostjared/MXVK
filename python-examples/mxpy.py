#!/usr/bin/env python3

import os
import runpy
import sys
from pathlib import Path

def find_module_dir():
    launcher_dir = Path(__file__).resolve().parent

    candidates = [
        launcher_dir / "python_mod",
        launcher_dir.parent / "python_mod",
        Path.cwd() / "python_mod",
    ]

    env_dir = os.environ.get("MXVK_PYTHON_MODULE_DIR")

    if(env_dir):
        candidates.insert(0, Path(env_dir))

    for directory in candidates:
        if(directory.exists() and any(directory.glob("mxvk_ext*.pyd"))):
            return directory
        if(directory.exists() and any(directory.glob("mxvk_ext*.so"))):
            return directory

    return None

def find_vcpkg_bin():
    vcpkg_root = os.environ.get("VCPKG_ROOT")

    if(vcpkg_root):
        root = Path(vcpkg_root) / "installed"

        if(root.exists()):
            for triplet in root.iterdir():
                bin_dir = triplet / "bin"

                if(bin_dir.exists()):
                    return bin_dir

    executable = Path(sys.executable).resolve()
    parts = executable.parts

    try:
        installed_index = parts.index("installed")
    except ValueError:
        return None

    if(installed_index + 1 >= len(parts)):
        return None

    triplet = parts[installed_index + 1]
    vcpkg_root = Path(*parts[:installed_index])
    bin_dir = vcpkg_root / "installed" / triplet / "bin"

    if(bin_dir.exists()):
        return bin_dir

    return None

def find_cuda_bin():
    cuda_path = os.environ.get("CUDA_PATH")

    if(not cuda_path):
        return None

    cuda_root = Path(cuda_path)
    x64_bin = cuda_root / "bin" / "x64"
    bin_dir = cuda_root / "bin"

    if(x64_bin.exists()):
        return x64_bin

    if(bin_dir.exists()):
        return bin_dir

    return None

def main():
    if(len(sys.argv) < 2):
        print(f"Usage: {Path(sys.argv[0]).name} script.py [arguments...]")
        return 1

    script = Path(sys.argv[1]).resolve()

    if(not script.is_file()):
        print(f"Script not found: {script}")
        return 1

    module_dir = find_module_dir()

    if(module_dir is None):
        print("Could not locate the MXVK Python module.")
        print("Set MXVK_PYTHON_MODULE_DIR to the directory containing mxvk_ext.")
        return 1

    sys.path.insert(0, str(module_dir))

    dll_handles = []

    if(os.name == "nt"):
        dll_dirs = [
            module_dir,
            find_vcpkg_bin(),
            find_cuda_bin(),
        ]

        for directory in dll_dirs:
            if(directory is not None and directory.exists()):
                dll_handles.append(os.add_dll_directory(str(directory)))

    sys.argv = [str(script)] + sys.argv[2:]

    try:
        runpy.run_path(str(script), run_name="__main__")
    except KeyboardInterrupt:
        return 130

    return 0

if(__name__ == "__main__"):
    raise SystemExit(main())