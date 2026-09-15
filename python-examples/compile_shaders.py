#!/usr/bin/env python3
## @file compile_shaders.py
## @brief Regenerate all SPIR-V shader assets used by the Python examples.
## @details Uses @c glslc by default and writes each shader beside the example
## data that loads it. Pass @c --glslc to select another compiler executable.
## @section compile_python_shaders_usage Usage
## @code{.sh}
## python3 python-examples/compile_shaders.py
## python3 python-examples/compile_shaders.py --glslc /opt/vulkan/bin/glslc
## @endcode

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys


def parse_args() -> argparse.Namespace:
    ## @brief Parse the optional GLSL compiler path.
    parser = argparse.ArgumentParser(description="Compile every Python-example shader to its required SPIR-V path.")
    parser.add_argument("--glslc", default="glslc", help="glslc executable or absolute path (default: glslc)")
    parser.add_argument("--dry-run", action="store_true", help="list shader jobs without writing SPIR-V files")
    return parser.parse_args()


def shader_jobs(root: Path) -> list[tuple[Path, Path]]:
    ## @brief Return every source/output pair required by Python examples.
    ## @param root The @c python-examples directory.
    ## @return Ordered GLSL source and SPIR-V output path pairs.
    shared = root.parent / "mxvk" / "shaders"
    return [
        (root / "sprite/shaders/vertex.vert", root / "sprite/data/sprite.vert.spv"),
        (root / "sprite/shaders/fragment.frag", root / "sprite/data/fragment.frag.spv"),
        (root / "compute/shaders/vertex.vert", root / "compute/data/sprite.vert.spv"),
        (root / "compute/shaders/fragment.frag", root / "compute/data/fragment.frag.spv"),
        (root / "compute/shaders/compute.comp", root / "compute/data/compute.comp.spv"),
        (root / "compute/shaders/inter.comp", root / "compute/data/inter.comp.spv"),
        (root / "asteroids/data/vertex.vert", root / "asteroids/data/vert.spv"),
        (root / "asteroids/data/fragment.frag", root / "asteroids/data/frag.spv"),
        (root / "asteroids/data/solid_frag.frag", root / "asteroids/data/solid_frag.spv"),
        (root / "asteroids/data/sprite_vertex.vert", root / "asteroids/data/sprite_vert.spv"),
        (root / "asteroids/data/sprite_fragment.frag", root / "asteroids/data/sprite_frag.spv"),
        (root / "knight/shaders/sprite.vert", root / "knight/data/sprite.vert.spv"),
        (root / "knight/shaders/sprite.frag", root / "knight/data/sprite.frag.spv"),
        (root / "knight/shaders/text.vert", root / "knight/data/text.vert.spv"),
        (root / "knight/shaders/text.frag", root / "knight/data/text.frag.spv"),
        (root / "knight/shaders/color_key.frag", root / "knight/data/color_key.frag.spv"),
        (root / "knight/shaders/fade.frag", root / "knight/data/fade.frag.spv"),
        (root / "darkside/shaders/dark.vert", root / "darkside/data/dark.vert.spv"),
        (root / "darkside/shaders/dark.frag", root / "darkside/data/dark.frag.spv"),
        (root / "darkside/shaders/beam3d.vert", root / "darkside/data/beam3d.vert.spv"),
        (root / "darkside/shaders/beam3d.frag", root / "darkside/data/beam3d.frag.spv"),
        (root / "darkside/shaders/beam.frag", root / "darkside/data/beam.frag.spv"),
        (root / "darkside/shaders/gradient_mix.frag", root / "darkside/data/gradient_mix.frag.spv"),
        (root / "model/shaders/model.vert", root / "model/data/model.vert.spv"),
        (root / "model/shaders/model.frag", root / "model/data/model.frag.spv"),
        (root / "model/shaders/beam3d.vert", root / "model/data/beam3d.vert.spv"),
        (root / "model/shaders/beam3d.frag", root / "model/data/beam3d.frag.spv"),
        (root / "model/shaders/beam.frag", root / "model/data/beam.frag.spv"),
        (root / "model/shaders/dark.vert", root / "model/data/dark.vert.spv"),
        (root / "model/shaders/dark.frag", root / "model/data/dark.frag.spv"),
        (root / "model/shaders/gradient_mix.frag", root / "model/data/gradient_mix.frag.spv"),
        (root / "penguin/shaders/model.vert", root / "penguin/data/model.vert.spv"),
        (root / "penguin/shaders/model.frag", root / "penguin/data/model.frag.spv"),
        (root / "penguin/shaders/beam3d.vert", root / "penguin/data/beam3d.vert.spv"),
        (root / "penguin/shaders/beam3d.frag", root / "penguin/data/beam3d.frag.spv"),
        (root / "penguin/shaders/beam.frag", root / "penguin/data/beam.frag.spv"),
        (root / "penguin/shaders/dark.vert", root / "penguin/data/dark.vert.spv"),
        (root / "penguin/shaders/dark.frag", root / "penguin/data/dark.frag.spv"),
        (root / "penguin/shaders/gradient_mix.frag", root / "penguin/data/gradient_mix.frag.spv"),
        (root / "opencv_mxwrite_shader/shaders/compute.comp", root / "opencv_mxwrite_shader/data/compute.comp.spv"),
        (root / "opencv_mxwrite_shader/shaders/inter.comp", root / "opencv_mxwrite_shader/data/inter.comp.spv"),
        (shared / "sprite.vert", root / "opencv_mxwrite_shader/data/sprite.vert.spv"),
        (shared / "sprite.frag", root / "opencv_mxwrite_shader/data/fragment.frag.spv"),
        (root / "mxvk_wrap/examples/tetris/data/background.frag", root / "mxvk_wrap/examples/tetris/data/background.frag.spv"),
        (shared / "sprite.vert", root / "mxvk_wrap/examples/tetris/data/sprite.vert.spv"),
    ]


def compile_shader(compiler: str, source: Path, output: Path) -> None:
    ## @brief Compile one GLSL shader to SPIR-V.
    ## @param compiler The @c glslc executable.
    ## @param source GLSL shader source file.
    ## @param output Destination SPIR-V file.
    output.parent.mkdir(parents=True, exist_ok=True)
    print(f"{source} -> {output}")
    subprocess.run([compiler, str(source), "-o", str(output)], check=True)


def main() -> int:
    ## @brief Compile the complete Python-example shader manifest.
    args = parse_args()
    root = Path(__file__).resolve().parent
    compiler_path = Path(args.glslc).expanduser()
    compiler = shutil.which(args.glslc) if compiler_path.parent == Path(".") else str(compiler_path)
    if compiler is None or (compiler_path.parent != Path(".") and not compiler_path.is_file()):
        print(f"glslc compiler not found: {args.glslc}", file=sys.stderr)
        return 2
    jobs = shader_jobs(root)
    for source, output in jobs:
        if not source.is_file():
            print(f"shader source not found: {source}", file=sys.stderr)
            return 2
        if args.dry_run:
            print(f"{source} -> {output}")
            continue
        try:
            compile_shader(compiler, source, output)
        except subprocess.CalledProcessError:
            return 1
    print(f"{'Would compile' if args.dry_run else 'Compiled'} {len(jobs)} Python-example shaders.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
