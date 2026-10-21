#!/usr/bin/env python3
"""Compile the local copies of the native Tetris shaders."""

from pathlib import Path
import shutil
import subprocess


def compile_shaders():
    root = Path(__file__).resolve().parent
    sources = sorted(path for path in (root / "shaders").iterdir() if path.suffix in (".vert", ".frag"))
    stale = [(path, root / "data" / (path.name + ".spv")) for path in sources if not (root / "data" / (path.name + ".spv")).exists() or path.stat().st_mtime > (root / "data" / (path.name + ".spv")).stat().st_mtime]
    if stale and not shutil.which("glslc"):
        raise RuntimeError("Install glslc from the Vulkan SDK and run tetris3d/compile_shaders.py")
    for source, output in stale:
        subprocess.run(["glslc", str(source), "-o", str(output)], check=True)
    return len(stale)


if __name__ == "__main__":
    print(f"Compiled {compile_shaders()} Tetris shaders.")
