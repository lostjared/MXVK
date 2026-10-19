#!/usr/bin/env python3
"""Compile the bundled original shaders with the Vulkan SDK's glslc."""
from pathlib import Path
import shutil
import subprocess


def compile_shaders():
    root = Path(__file__).resolve().parent / "shaders"
    sources = sorted(path for path in root.rglob("*") if path.suffix in (".vert", ".frag", ".glsl"))
    stale = [path for path in sources if not Path(str(path) + ".spv").exists() or path.stat().st_mtime > Path(str(path) + ".spv").stat().st_mtime]
    if stale and not shutil.which("glslc"):
        raise RuntimeError("Install glslc from the Vulkan SDK, then run mutatris/compile_shaders.py")
    for path in stale:
        command = ["glslc"]
        if path.suffix == ".glsl":
            command.append("-fshader-stage=frag")
        subprocess.run(command + [str(path), "-o", str(path) + ".spv"], check=True)
    return len(stale)


if __name__ == "__main__":
    print(f"Compiled {compile_shaders()} Mutatris shaders.")
