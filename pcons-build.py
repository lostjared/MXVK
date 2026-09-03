#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["pcons>=0.24"]
# ///
"""Native pcons build for MXVK.

Usage:
    uvx pcons -B build/pcons --reconfigure
    uvx pcons -B build/pcons EXAMPLES=0
    uvx pcons -B build/pcons WITH_CUDA=OFF WITH_MXWRITE=OFF

Options accept ON/OFF/AUTO where noted and mirror the root CMake project:
DEBUG_MODE, VALIDATION, CV, JPEG, EXAMPLES, FRACTAL_ZOOM,
WITH_CUDA, WITH_EIGEN, WITH_MXWRITE, and WITH_MIXER.
"""

import os
import shlex
from pathlib import Path

from pcons import (
    ImportedTarget,
    PackageDescription,
    Project,
    Target,
    configure_file,
    find_c_toolchain,
    get_platform,
    get_var,
)

VERSION = "0.33.1"
project_dir = Path(__file__).parent.resolve()
platform = get_platform()


def configure_homebrew_paths() -> None:
    """Make direct macOS Pcons invocations see Homebrew's keg-only packages."""
    if not platform.is_macos:
        return
    package_dirs: list[str] = []
    binary_dirs: list[str] = []
    for prefix in (Path("/opt/homebrew"), Path("/usr/local")):
        package_dirs.extend(
            str(path)
            for path in (prefix / "lib" / "pkgconfig", prefix / "share" / "pkgconfig")
            if path.is_dir()
        )
        opt_dir = prefix / "opt"
        if opt_dir.is_dir():
            package_dirs.extend(str(path) for path in opt_dir.glob("*/lib/pkgconfig") if path.is_dir())
            package_dirs.extend(str(path) for path in opt_dir.glob("*/share/pkgconfig") if path.is_dir())
            binary_dirs.extend(str(path) for path in opt_dir.glob("*/bin") if path.is_dir())
    if package_dirs:
        os.environ["PKG_CONFIG_PATH"] = os.pathsep.join(
            package_dirs + [os.environ.get("PKG_CONFIG_PATH", "")]
        )
    if binary_dirs:
        os.environ["PATH"] = os.pathsep.join(binary_dirs + [os.environ.get("PATH", "")])


configure_homebrew_paths()


def option(name: str, default: bool = False) -> bool:
    return get_var(name, "1" if default else "0").lower() in (
        "1",
        "on",
        "true",
        "yes",
    )


def tristate(name: str, default: str = "AUTO") -> str:
    value = get_var(name, default).upper()
    if value not in ("AUTO", "ON", "OFF"):
        raise SystemExit(f"{name} must be AUTO, ON, or OFF (got {value!r})")
    return value


debug_mode = option("DEBUG_MODE")
validation = option("VALIDATION")
with_cv = option("CV")
with_jpeg = option("JPEG")
with_examples = option("EXAMPLES", default=True)
with_fractal = option("FRACTAL_ZOOM")
cuda_request = tristate("WITH_CUDA")
eigen_request = tristate("WITH_EIGEN")
mxwrite_request = tristate("WITH_MXWRITE")
mixer_request = tristate("WITH_MIXER")

extra_prefixes = [Path(p) for p in (get_var("PREFIX") or "").split(os.pathsep) if p]
if extra_prefixes:
    os.environ["PKG_CONFIG_PATH"] = os.pathsep.join(
        [str(p / "lib" / "pkgconfig") for p in extra_prefixes]
        + [os.environ.get("PKG_CONFIG_PATH", "")]
    )
search_prefixes = extra_prefixes + [
    Path("/opt/homebrew"),
    Path("/usr/local"),
    Path("/usr"),
    Path("/opt/cuda"),
]

project = Project("mxvk", root_dir=project_dir)
env = project.Environment(toolchain=find_c_toolchain())
env.set_variant("debug" if debug_mode else get_var("VARIANT", "release"))
env.cxx.set_standard(20)
env.cc.flags.extend(["-std=c23", "-Wall", "-pedantic"])
env.cxx.flags.extend(["-Wall", "-pedantic"])
if not debug_mode and get_var("VARIANT", "release").lower() == "release":
    env.cc.flags.append("-O3")
    env.cxx.flags.append("-O3")
if platform.is_linux:
    env.cc.flags.append("-fPIC")
    env.cxx.flags.append("-fPIC")
    env.cc.defines.append("_POSIX_C_SOURCE=200809L")
if validation:
    env.cc.defines.append("ENABLE_VALIDATION")
    env.cxx.defines.append("ENABLE_VALIDATION")
if platform.is_macos:
    # Match MXVK_ENABLE_MOLTENVK=ON from CMake.  These definitions enable
    # VK_KHR_portability_enumeration on the instance and request
    # VK_KHR_portability_subset when creating the MoltenVK device.
    env.cc.defines.extend(["MXVK_USE_MOLTENVK", "VK_ENABLE_BETA_EXTENSIONS"])
    env.cxx.defines.extend(["MXVK_USE_MOLTENVK", "VK_ENABLE_BETA_EXTENSIONS"])


def imported(
    name: str,
    *,
    include_dirs: list[str] | None = None,
    library_dirs: list[str] | None = None,
    libraries: list[str] | None = None,
    compile_flags: list[str] | None = None,
) -> ImportedTarget:
    return ImportedTarget.from_package(
        PackageDescription(
            name=name,
            include_dirs=include_dirs or [],
            library_dirs=library_dirs or [],
            libraries=libraries or [],
            compile_flags=compile_flags or [],
        )
    )


def system_headers(target: Target) -> Target:
    """Treat dependency headers as system headers so -Wall reports our code."""
    include_dirs = list(target.public.include_dirs)
    target.public.include_dirs.clear()
    for directory in include_dirs:
        # GCC already searches /usr/include. Re-adding it with -isystem moves
        # it ahead of the compiler's C++ wrapper headers and breaks
        # libstdc++'s #include_next <stdlib.h>.
        if Path(directory).resolve() == Path("/usr/include"):
            continue
        target.public.compile_flags.extend(["-isystem", str(directory)])
    return target


def find_header(probe: str) -> Path | None:
    for prefix in search_prefixes:
        candidate = prefix / "include" / probe
        if candidate.exists():
            return candidate.parent
    return None


def manual_library(
    name: str, header_dir: str, library: str, *, required: bool = True
) -> ImportedTarget | None:
    include_dir = find_header(header_dir)
    found_library_dir = None
    for prefix in search_prefixes:
        for directory in (prefix / "lib", prefix / "lib64"):
            if any((directory / f"lib{library}{suffix}").exists() for suffix in (".so", ".a", ".dylib")):
                found_library_dir = directory
                break
        if found_library_dir:
            break
    if include_dir and found_library_dir:
        return system_headers(
            imported(
                name,
                include_dirs=[str(include_dir)],
                library_dirs=[str(found_library_dir)],
                libraries=[library],
            )
        )
    if required:
        raise SystemExit(f"Missing dependency: {name} ({header_dir}, lib{library})")
    return None


def header_only(name: str, probe: str, *, required: bool = True) -> ImportedTarget | None:
    include_dir = find_header(probe)
    if include_dir:
        # find_header returns the probe's parent. Move back to the include root.
        root = include_dir
        for _ in Path(probe).parts[:-1]:
            root = root.parent
        return system_headers(imported(name, include_dirs=[str(root)]))
    if required:
        raise SystemExit(f"Missing dependency: {name} (include/{probe})")
    return None


def quoted(path: Path) -> str:
    return shlex.quote(str(path))


sdl3 = system_headers(project.find_package("sdl3"))
vulkan = system_headers(project.find_package("vulkan"))
libpng = system_headers(project.find_package("libpng"))
zlib = system_headers(project.find_package("zlib"))
sdl3_ttf = manual_library("SDL3_ttf", "SDL3_ttf/SDL_ttf.h", "SDL3_ttf")
glm = header_only("glm", "glm/glm.hpp")

sdl3_mixer = manual_library(
    "SDL3_mixer", "SDL3_mixer/SDL_mixer.h", "SDL3_mixer", required=False
)
with_mixer = mixer_request == "ON" or (mixer_request == "AUTO" and sdl3_mixer is not None)
if mixer_request == "ON" and sdl3_mixer is None:
    raise SystemExit("WITH_MIXER=ON requested, but SDL3_mixer was not found")

eigen = None
for eigen_prefix in search_prefixes:
    eigen_include = eigen_prefix / "include" / "eigen3"
    if (eigen_include / "Eigen" / "Dense").exists():
        eigen = system_headers(imported("Eigen3", include_dirs=[str(eigen_include)]))
        break
with_eigen = eigen_request == "ON" or (eigen_request == "AUTO" and eigen is not None)
if eigen_request == "ON" and eigen is None:
    raise SystemExit("WITH_EIGEN=ON requested, but Eigen3 was not found")

ffmpeg_packages: list[Target] = []
if mxwrite_request != "OFF":
    try:
        ffmpeg_packages = [
            system_headers(project.find_package(name))
            for name in ("libavcodec", "libavformat", "libavutil", "libswscale")
        ]
    except Exception:
        if mxwrite_request == "ON":
            raise
        ffmpeg_packages = []
with_mxwrite = bool(ffmpeg_packages)

opencv = None
cuda = None
cuda_root = Path(get_var("CUDA_PREFIX", "/opt/cuda"))
cuda_header = cuda_root / "include" / "cuda_runtime.h"
cuda_lib = cuda_root / "lib64" / "libcudart.so"
if not cuda_header.exists():
    cuda_header = cuda_root / "targets" / "x86_64-linux" / "include" / "cuda_runtime.h"
    cuda_lib = cuda_root / "targets" / "x86_64-linux" / "lib" / "libcudart.so"
cuda_available = cuda_header.exists() and cuda_lib.exists()
with_cuda = cuda_request == "ON" or (cuda_request == "AUTO" and cuda_available)
if cuda_request == "ON" and not cuda_available:
    raise SystemExit(f"WITH_CUDA=ON requested, but CUDA was not found under {cuda_root}")
if with_cv or with_cuda:
    try:
        opencv = system_headers(project.find_package(get_var("OPENCV_PACKAGE", "opencv5")))
    except Exception:
        if with_cv or cuda_request == "ON":
            raise
        with_cuda = False
if with_cuda:
    cuda_include = cuda_header.parent
    cuda = imported(
        "CUDA",
        library_dirs=[str(cuda_lib.parent)],
        libraries=["cudart", "nppicc", "nppidei", "nppc"],
        compile_flags=["-isystem", str(cuda_include)],
    )

jpeg = system_headers(project.find_package("libjpeg")) if with_jpeg else None

version_h = configure_file(
    project_dir / "cmake" / "mxvk_version.hpp.in",
    project.build_dir / "mxvk" / "include" / "mxvk" / "mxvk_version.hpp",
    {
        "PROJECT_VERSION_MAJOR": "0",
        "PROJECT_VERSION_MINOR": "33",
        "PROJECT_VERSION_PATCH": "1",
    },
)

shader_targets: list[Target] = []
shader_output_dir = (project_dir / project.build_dir / "mxvk" / "shaders").resolve()
font_output_dir = (project_dir / project.build_dir / "mxvk" / "data").resolve()
font_output = font_output_dir / "default.ttf"
font_target = project.Command(
    "mxvk-default-font",
    env,
    target=font_output,
    source=project_dir / "mxvk" / "data" / "default.ttf",
    command=f"mkdir -p {quoted(font_output_dir)} && cp {quoted(project_dir / 'mxvk' / 'data' / 'default.ttf')} {quoted(font_output)}",
)
for shader in sorted((project_dir / "mxvk" / "shaders").glob("*")):
    if shader.suffix not in (".vert", ".frag", ".comp"):
        continue
    output = shader_output_dir / f"{shader.name}.spv"
    shader_targets.append(
        project.Command(
            f"shader-{shader.name}",
            env,
            target=output,
            source=shader,
            command=f"mkdir -p {shader_output_dir} && glslc {shader} -o {output}",
        )
    )

# Mutatris has its own post-processing shader pack. CMake generates these
# under examples/mutatris and copies them beside the executable; pcons uses a
# dedicated runtime tree because its executables are emitted flat.
mutatris_source_dir = project_dir / "examples" / "mutatris"
mutatris_runtime_dir = (
    project_dir / project.build_dir / "runtime" / "mutatris"
).resolve()
mutatris_shader_dir = mutatris_runtime_dir / "shaders"
mutatris_runtime_targets: list[Target] = []
mutatris_runtime_outputs: list[Path] = []

mutatris_shader_sources = [
    (project_dir / "mxvk" / "shaders" / "sprite.vert", "background.vert.spv", ""),
    (mutatris_source_dir / "shaders" / "background.frag", "background.frag.spv", ""),
    (mutatris_source_dir / "shaders" / "fade.frag", "fade.frag.spv", ""),
    (mutatris_source_dir / "shaders" / "crt.frag", "crt.frag.spv", ""),
]
for shader, output_name, extra_flags in mutatris_shader_sources:
    output = mutatris_shader_dir / output_name
    mutatris_runtime_outputs.append(output)
    mutatris_runtime_targets.append(
        project.Command(
            f"mutatris-shader-{output_name}",
            env,
            target=output,
            source=shader,
            command=(
                f"mkdir -p {mutatris_shader_dir} && "
                f"glslc {extra_flags} {shader} -o {output}"
            ),
        )
    )

mutatris_effect_dir = mutatris_shader_dir / "effects"
for shader in sorted((mutatris_source_dir / "shaders" / "effects").glob("*.glsl")):
    output = mutatris_effect_dir / f"{shader.name}.spv"
    mutatris_runtime_outputs.append(output)
    mutatris_runtime_targets.append(
        project.Command(
            f"mutatris-effect-{shader.stem}",
            env,
            target=output,
            source=shader,
            command=(
                f"mkdir -p {mutatris_effect_dir} && "
                f"glslc -fshader-stage=frag {shader} -o {output}"
            ),
        )
    )

mutatris_data_files = sorted(
    path for path in (mutatris_source_dir / "data").rglob("*") if path.is_file()
)
mutatris_data_marker = mutatris_runtime_dir / "data" / ".pcons-assets"
mutatris_runtime_targets.append(
    project.Command(
        "mutatris-runtime-data",
        env,
        target=mutatris_data_marker,
        source=mutatris_data_files,
        command=(
            f"mkdir -p {mutatris_runtime_dir / 'data'} && "
            f"cp -a {mutatris_source_dir / 'data'}/. "
            f"{mutatris_runtime_dir / 'data'}/ && touch {mutatris_data_marker}"
        ),
    )
)
mutatris_runtime_outputs.append(mutatris_data_marker)
mutatris_shader_marker = mutatris_runtime_dir / ".pcons-shaders"
mutatris_runtime_target = project.Command(
    "mutatris-runtime",
    env,
    target=mutatris_shader_marker,
    source=mutatris_runtime_outputs,
    command=f"touch {quoted(mutatris_shader_marker)}",
)

volk = project.StaticLibrary("volk", env, sources=[project_dir / "volk" / "volk.cpp"])
volk.public.include_dirs.append(project_dir)
volk.link(vulkan)
if platform.is_linux:
    volk.public.link_libs.append("dl")

mxnetwork = project.StaticLibrary(
    "mxnetwork",
    env,
    sources=[
        project_dir / "MXNetwork" / "src" / "mxsocket.c",
        project_dir / "MXNetwork" / "src" / "socket.cpp",
        project_dir / "MXNetwork" / "src" / "exception.cpp",
    ],
)
mxnetwork.public.include_dirs.append(project_dir / "MXNetwork" / "include")
mxnetwork.public.link_flags.append("-pthread")

mxwrite = None
if with_mxwrite:
    mxwrite = project.StaticLibrary(
        "mxwrite", env, sources=[project_dir / "MXWrite" / "mxwrite.cpp"]
    )
    mxwrite.public.include_dirs.append(project_dir / "MXWrite")
    mxwrite.public.defines.append("MXWRITE_ENABLED=1")
    mxwrite.private.compile_flags.append("-Wextra")
    mxwrite.link(*ffmpeg_packages)
    mxwrite.public.link_flags.append("-pthread")
    if cuda:
        mxwrite.public.defines.append("MXWRITE_HAS_CUDA_COPY=1")
        mxwrite.link(cuda)

core_sources = [
    "mxvk.cpp",
    "mxvk_console.cpp",
    "mxvk_controller.cpp",
    "mxvk_png.cpp",
    "mxvk_resource.cpp",
    "mxvk_shader_module.cpp",
    "mxvk_stencil.cpp",
    "mxvk_sprite.cpp",
    "mxvk_sprite3d.cpp",
    "mxvk_point_sprite_batch.cpp",
    "mxvk_text.cpp",
    "mxvk_model.cpp",
    "mxvk_abstract_model.cpp",
    "mxvk_cfg.cpp",
    "mxvk_io_window.cpp",
]
if with_mixer:
    core_sources.append("mxvk_sound.cpp")
if with_jpeg:
    core_sources.append("mxvk_jpeg.cpp")
if with_cv:
    core_sources.append("mxvk_cv.cpp")
if with_mxwrite:
    core_sources.append("mxvk_ff_capture.cpp")

mxvk = project.StaticLibrary(
    "mxvk", env, sources=[project_dir / "mxvk" / "src" / name for name in core_sources]
)
mxvk.public.include_dirs.extend(
    [project_dir / "mxvk" / "include", project.build_dir / "mxvk" / "include"]
)
mxvk.public.defines.extend(
    [
        f'MXVK_SPRITE_SHADER_DIR="{shader_output_dir}"',
        f'MXVK_TEXT_SHADER_DIR="{shader_output_dir}"',
        f'MXVK_SPRITE3D_SHADER_DIR="{shader_output_dir}"',
        f'MXVK_DEFAULT_FONT_DIR="{font_output_dir}"',
    ]
)
mxvk.link(volk, sdl3, sdl3_ttf, vulkan, libpng, zlib, glm)
if with_mixer and sdl3_mixer:
    mxvk.public.defines.extend(["MXVK_WITH_MIXER", "WITH_MIXER"])
    mxvk.link(sdl3_mixer)
if with_jpeg and jpeg:
    mxvk.public.defines.extend(["MXVK_WITH_JPEG", "WITH_JPEG"])
    mxvk.link(jpeg)
if with_cv and opencv:
    mxvk.public.defines.append("MXVK_WITH_CV")
    mxvk.link(opencv)
if with_cuda and cuda and opencv:
    mxvk.public.defines.extend(["MXVK_CUDA", "MXVK_CUDA_NPP"])
    mxvk.link(cuda, opencv)
if with_mxwrite and mxwrite:
    mxvk.public.defines.append("MXVK_WITH_FFMPEG_CAPTURE")
    mxvk.link(mxwrite)
mxvk.add_dependency(font_target, *shader_targets)

mxmod2obj = project.Program(
    "mxmod2obj", env, sources=[project_dir / "tools" / "mxmod2obj.cpp"]
)
mxmod2obj.link(mxvk)

EXAMPLES: list[tuple[str, str, list[str]]] = [
    ("skeleton", "skeleton", ["skeleton.cpp"]),
    ("surface", "surface", ["surface.cpp"]),
    ("stencil_surface", "stencil_surface", ["surface.cpp"]),
    ("hello_world", "hello_world", ["main.cpp"]),
    ("static_example", "static_example", ["main.cpp"]),
    ("stencil", "stencil", ["stencil.cpp"]),
    ("sprite_example", "sprite_example", ["main.cpp"]),
    ("3dmath", "3dmath", ["main.cpp"]),
    ("3dmath_cube", "3dmath_cube", ["main.cpp"]),
    ("3dmath_pong", "3dmath_pong", ["main.cpp"]),
    ("3dmath_puzzle_drop", "3dmath_puzzle_drop", ["main.cpp"]),
    ("3dmath_pyramid", "3dmath_pyramid", ["main.cpp"]),
    ("3dmath_plg_loader", "3dmath_plg_loader", ["main.cpp"]),
    ("3dmath_obj_loader", "3dmath_obj_loader", ["../3dmath_plg_loader/main.cpp"]),
    ("3dmath_texture", "3dmath_texture", ["main.cpp"]),
    ("3dmath_texture_array", "3dmath_texture_array", ["main.cpp"]),
    ("sprite3d_example", "sprite3d_example", ["example.cpp"]),
    ("text_example", "text_example", ["main.cpp"]),
    ("model_example", "model_example", ["main.cpp"]),
    ("viewer", "viewer", ["viewer.cpp"]),
    ("dark", "dark", ["dark.cpp"]),
    ("moon", "moon", ["moon.cpp"]),
    ("starship", "starship", ["starship.cpp"]),
    ("starfield", "starfield", ["starfield.cpp"]),
    ("pointsprite", "pointsprite", ["point.cpp"]),
    ("fireworks", "fireworks", ["fireworks.cpp"]),
    ("fire", "fire", ["fire.cpp"]),
    ("planet", "planet", ["planet.cpp"]),
    ("tux_example", "tux_example", ["main.cpp"]),
    ("glitch_cube", "glitch_cube", ["glitch.cpp"]),
    ("asteroids", "asteroids", ["space.cpp"]),
    ("asteroids3d", "asteroids3d", ["main.cpp", "asteroids3d_window.cpp", "ship.cpp", "starfield.cpp"]),
    ("asteroids-net", "asteroids-net", ["main.cpp", "multiplayer.cpp", "port_mapping.cpp", "asteroids3d_window.cpp", "ship.cpp", "starfield.cpp"]),
    ("defender", "defender", ["defender.cpp", "defender_assets.cpp", "defender_combat.cpp", "defender_console.cpp", "defender_enemies.cpp", "defender_flame.cpp", "defender_intro.cpp", "defender_window.cpp", "../asteroids3d/ship.cpp", "../asteroids3d/starfield.cpp"]),
    ("pong", "pong", ["pong.cpp"]),
    ("breakout", "breakout", ["breakout.cpp"]),
    ("tetris", "tetris", ["tetris.cpp"]),
    ("puzzle_drop", "puzzle_drop", ["puzzle_drop.cpp"]),
    ("mutatris", "mutatris", ["game_grid.cpp", "mutatris.cpp", "mutatris_window.cpp", "piece.cpp", "puzzle_game.cpp"]),
    ("tictactoe", "tictactoe", ["main.cpp"]),
    ("knight", "knight", ["knight.cpp"]),
    ("puzzle", "puzzle", ["acid.drop.cpp"]),
    ("pool_demo", "3DPool", ["main.cpp"]),
    ("masterpiece", "MasterPiece", ["main.cpp"]),
    ("3dmath_masterpiece", "3dmath_masterpiece", ["main.cpp"]),
    ("console_demo", "console_demo", ["main.cpp"]),
    ("postprocess", "postprocess", ["post.cpp"]),
    ("cfg_example", "cfg_example", ["main.cpp"]),
    ("matrix", "matrix", ["matrix.cpp"]),
    ("binary_matrix", "binary_matrix", ["binary_matrix.cpp"]),
    ("walk", "walk_example", ["room.cpp"]),
    ("walk_post", "walk_post", ["room.cpp"]),
    ("bluesky", "bluesky", ["bluesky.cpp"]),
]
if with_fractal:
    EXAMPLES.append(("fractal_zoom", "fractal_zoom", ["fractal.cpp"]))
if with_cv:
    EXAMPLES.extend(
        [
            ("compute_shader", "compute_shader", ["main.cpp"]),
            ("opencv_example", "opencv_example", ["main.cpp"]),
            ("shader_viewer", "shader_viewer", ["shaders.cpp"]),
            ("opencv_model", "opencv_model", ["main.cpp"]),
        ]
    )

asset_defines = {
    "skeleton": "skeleton_ASSET_DIR",
    "surface": "skeleton_ASSET_DIR",
    "stencil_surface": "skeleton_ASSET_DIR",
    "hello_world": "HELLO_WORLD_ASSET_DIR",
    "static_example": "static_example_ASSET_DIR",
    "stencil": "STENCIL_ASSET_DIR",
    "sprite_example": "sprite_example_ASSET_DIR",
    "3dmath_puzzle_drop": "math3d_puzzle_drop_ASSET_DIR",
    "sprite3d_example": "sprite3d_example_ASSET_DIR",
    "text_example": "text_example_ASSET_DIR",
    "model_example": "MODEL_EXAMPLE_ASSET_DIR",
    "viewer": "VIEWER_ASSET_DIR",
    "dark": "DARK_ASSET_DIR",
    "moon": "MOON_ASSET_DIR",
    "starship": "STARSHIP_EXAMPLE_ASSET_DIR",
    "starfield": "STARFIELD_ASSET_DIR",
    "pointsprite": "POINTSPRITE_ASSET_DIR",
    "fireworks": "FIREWORKS_ASSET_DIR",
    "fire": "FIRE_ASSET_DIR",
    "planet": "PLANET_ASSET_DIR",
    "tux_example": "tux_example_ASSET_DIR",
    "glitch_cube": "GLITCH_CUBE_ASSET_DIR",
    "asteroids": "asteroids_ASSET_DIR",
    "asteroids3d": "ASTEROIDS3D_ASSET_DIR",
    "asteroids-net": "ASTEROIDS3D_ASSET_DIR",
    "defender": "DEFENDER_ASSET_DIR",
    "pong": "pong_ASSET_DIR",
    "breakout": "breakout_ASSET_DIR",
    "tetris": "tetris_ASSET_DIR",
    "puzzle_drop": "puzzle_drop_ASSET_DIR",
    "mutatris": "mutatris_ASSET_DIR",
    "tictactoe": "tictactoe_ASSET_DIR",
    "knight": "KNIGHT_ASSET_DIR",
    "puzzle": "puzzle_ASSET_DIR",
    "pool_demo": "POOL_DEMO_ASSET_DIR",
    "masterpiece": "MASTERPIECE_ASSET_DIR",
    "3dmath_masterpiece": "MASTERPIECE_ASSET_DIR",
    "console_demo": "console_demo_ASSET_DIR",
    "postprocess": "postprocess_ASSET_DIR",
    "matrix": "matrix_ASSET_DIR",
    "binary_matrix": "binary_matrix_ASSET_DIR",
    "walk": "WALK_ASSET_DIR",
    "walk_post": "WALK_ASSET_DIR",
    "bluesky": "WATER_ASSET_DIR",
    "compute_shader": "compute_shader_ASSET_DIR",
    "opencv_example": "opencv_example_ASSET_DIR",
    "shader_viewer": "shader_viewer_ASSET_DIR",
    "opencv_model": "opencv_model_ASSET_DIR",
}

# =============================================================================
# Per-example shader and runtime trees
# =============================================================================

# These are legacy OpenGL/OpenGL ES shaders retained as source assets. They
# are not valid Vulkan GLSL and are not compiled by the CMake build either.
LEGACY_NON_VULKAN_SHADERS = {
    "examples/asteroids-net/data/text.frag",
    "examples/asteroids-net/data/text.vert",
    "examples/asteroids3d/data/text.frag",
    "examples/asteroids3d/data/text.vert",
    "examples/breakout/data/text.frag",
    "examples/breakout/data/text.vert",
    "examples/breakout/data/tri.frag",
    "examples/breakout/data/tri.vert",
    "examples/glitch_cube/data/tri.frag",
    "examples/glitch_cube/data/tri.vert",
    "examples/walk/data/text.frag",
    "examples/walk/data/text.vert",
    "examples/walk_post/data/text.frag",
    "examples/walk_post/data/text.vert",
}

# Some demos intentionally reuse shaders owned by another directory.
EXTRA_DEMO_SHADER_SOURCES = {
    "defender": [
        "examples/asteroids3d/shaders/model.vert",
        "examples/asteroids3d/shaders/model.frag",
        "examples/asteroids3d/shaders/flame.vert",
        "examples/asteroids3d/shaders/flame.frag",
        "examples/asteroids3d/shaders/intro.frag",
        "examples/asteroids3d/shaders/fade_overlay.frag",
        "examples/asteroids3d/shaders/crt.frag",
    ],
    "moon": [
        "examples/model_example/shaders/model.vert",
        "examples/model_example/shaders/model.frag",
    ],
    "shader_viewer": [
        "examples/opencv_example/shaders/vertex.vert",
        "examples/opencv_example/shaders/fragment.frag",
    ],
    "tetris": ["mxvk/shaders/sprite.vert"],
}

# Runtime files that CMake copies from another demo or a shared repository
# directory. Local data/ and shaders/ directories are staged automatically.
EXTRA_DEMO_RUNTIME_DIRECTORIES = {
    "3dmath_puzzle_drop": [("examples/puzzle_drop/data", "data")],
    "defender": [("examples/asteroids3d/data", "data")],
    "shader_viewer": [("examples/opencv_example/data", "data")],
}

EXTRA_DEMO_RUNTIME_FILES = {
    "3dmath_obj_loader": [
        ("models/obj/sphere.obj", "data/sphere.obj"),
        ("models/obj/sphere.mtl", "data/sphere.mtl"),
    ],
    "3dmath_puzzle_drop": [
        ("examples/tictactoe/data/font.ttf", "data/font.ttf")
    ],
    "asteroids": [("examples/asteroids/font.ttf", "data/font.ttf")],
    "asteroids3d": [
        ("examples/rain/data/NotoSansCJK-Bold.ttc", "data/NotoSansCJK-Bold.ttc"),
        ("examples/rain/data/NotoSansCJK-Regular.ttc", "data/NotoSansCJK-Regular.ttc"),
        ("examples/defender/data/crash.wav", "data/crash.wav"),
        ("examples/defender/data/asteroid.wav", "data/asteroid.wav"),
    ],
    "asteroids-net": [
        ("examples/rain/data/NotoSansCJK-Bold.ttc", "data/NotoSansCJK-Bold.ttc"),
        ("examples/rain/data/NotoSansCJK-Regular.ttc", "data/NotoSansCJK-Regular.ttc"),
        ("examples/defender/data/crash.wav", "data/crash.wav"),
        ("examples/defender/data/asteroid.wav", "data/asteroid.wav"),
    ],
    "binary_matrix": [
        ("examples/rain/data/NotoSansCJK-Bold.ttc", "data/NotoSansCJK-Bold.ttc"),
        ("examples/rain/data/NotoSansCJK-Regular.ttc", "data/NotoSansCJK-Regular.ttc"),
        ("examples/rain/data/LICENSE.noto-fonts-cjk.txt", "data/LICENSE.noto-fonts-cjk.txt"),
        ("examples/matrix/data/bg.png", "data/bg.png"),
    ],
    "compute_shader": [("examples/compute_shader/font.ttf", "data/font.ttf")],
    "defender": [
        ("examples/rain/data/NotoSansCJK-Bold.ttc", "data/NotoSansCJK-Bold.ttc"),
        ("examples/rain/data/NotoSansCJK-Regular.ttc", "data/NotoSansCJK-Regular.ttc"),
    ],
    "matrix": [
        ("examples/rain/data/NotoSansCJK-Bold.ttc", "data/NotoSansCJK-Bold.ttc"),
        ("examples/rain/data/NotoSansCJK-Regular.ttc", "data/NotoSansCJK-Regular.ttc"),
    ],
    "model_example": [
        ("examples/rain/data/NotoSansCJK-Bold.ttc", "data/NotoSansCJK-Bold.ttc"),
        ("examples/rain/data/NotoSansCJK-Regular.ttc", "data/NotoSansCJK-Regular.ttc"),
    ],
    "moon": [("examples/asteroids3d/data/star.png", "data/star.png")],
    "planet": [
        ("examples/rain/data/NotoSansCJK-Bold.ttc", "data/NotoSansCJK-Bold.ttc"),
        ("examples/rain/data/NotoSansCJK-Regular.ttc", "data/NotoSansCJK-Regular.ttc"),
    ],
    "pool_demo": [("examples/pool_demo/font.ttf", "font.ttf")],
    "puzzle_drop": [
        ("examples/rain/data/NotoSansCJK-Bold.ttc", "data/NotoSansCJK-Bold.ttc"),
        ("examples/rain/data/NotoSansCJK-Regular.ttc", "data/NotoSansCJK-Regular.ttc"),
        ("examples/tictactoe/data/font.ttf", "data/font.ttf"),
        ("examples/tetris/data/cube.mxmod.z", "data/cube.mxmod.z"),
        ("examples/tetris/data/manifest_gray.txt", "data/manifest_gray.txt"),
        ("examples/tetris/data/block_gray.png", "data/block_gray.png"),
    ],
    "starship": [("examples/pong/data/star.png", "data/star.png")],
    "tetris": [("examples/tictactoe/data/font.ttf", "data/font.ttf")],
    "walk": [
        ("models/cube.mxmod.z", "data/cube.mxmod.z"),
        ("models/sphere.mxmod.z", "data/sphere.mxmod.z"),
    ],
    "walk_post": [
        ("models/cube.mxmod.z", "data/cube.mxmod.z"),
        ("models/sphere.mxmod.z", "data/sphere.mxmod.z"),
    ],
}

# CMake gives these shaders a runtime name different from source-name + .spv.
# The generic pipeline also emits the normal name, which is useful to shader
# viewers and keeps every source independently addressable.
SPECIAL_SHADER_OUTPUTS = {
    ("breakout", "examples/breakout/data/model.vert"): ["shaders/breakout_model.vert.spv"],
    ("breakout", "examples/breakout/data/model.frag"): ["shaders/breakout_model.frag.spv"],
    ("breakout", "examples/breakout/data/background.frag"): ["shaders/breakout_background.frag.spv"],
    ("compute_shader", "examples/compute_shader/data/compute.comp"): ["compute.spv"],
    ("compute_shader", "examples/compute_shader/data/xorblend.comp"): ["xorblend.spv"],
    ("compute_shader", "examples/compute_shader/data/metalmedianblend.comp"): ["metalmedianblend.spv"],
    ("compute_shader", "examples/compute_shader/data/square_block_resize_dir.comp"): ["square_block_resize_dir.spv"],
    ("compute_shader", "examples/compute_shader/data/acidcam_filters.comp"): ["acidcam_filters.spv"],
    ("dark", "examples/dark/shaders/beam3d.frag"): ["shaders/beam.frag.spv"],
    ("opencv_model", "examples/opencv_model/shaders/vertex.vert"): ["shaders/model.vert.spv"],
    ("opencv_model", "examples/opencv_model/shaders/fragment.frag"): ["shaders/model.frag.spv"],
    ("pool_demo", "examples/pool_demo/data/vertex.vert"): ["shaders/model.vert.spv"],
    ("pool_demo", "examples/pool_demo/data/fragment.frag"): ["shaders/model.frag.spv"],
    ("pool_demo", "examples/pool_demo/data/sprite_vertex.vert"): ["shaders/sprite.vert.spv"],
    ("pong", "examples/pong/data/model.vert"): ["shaders/pong_model.vert.spv"],
    ("pong", "examples/pong/data/model.frag"): ["shaders/pong_model.frag.spv"],
    ("sprite_example", "examples/sprite_example/shaders/vertex.vert"): ["shaders/sprite.vert.spv"],
    ("stencil", "examples/stencil/shaders/fullscreen.vert"): [
        "shaders/content_fullscreen.vert.spv"
    ],
    ("tetris", "examples/tetris/data/model.vert"): ["shaders/tetris_model.vert.spv"],
    ("tetris", "examples/tetris/data/model.frag"): ["shaders/tetris_model.frag.spv"],
    ("tetris", "examples/tetris/data/tetris_piece.vert"): ["shaders/tetris_piece.vert.spv"],
    ("tetris", "examples/tetris/data/tetris_piece.frag"): ["shaders/tetris_piece.frag.spv"],
    ("tetris", "mxvk/shaders/sprite.vert"): ["shaders/tetris_intro.vert.spv"],
    ("tetris", "examples/tetris/data/intro.frag"): ["shaders/tetris_intro.frag.spv"],
    ("tetris", "examples/tetris/data/screen_fade.frag"): [
        "shaders/tetris_screen_fade.frag.spv"
    ],
    ("tetris", "examples/tetris/data/background_transition.frag"): [
        "shaders/tetris_background_transition.frag.spv"
    ],
}


def unique_paths(paths: list[Path]) -> list[Path]:
    result = []
    seen = set()
    for path in paths:
        key = str(path)
        if key not in seen:
            seen.add(key)
            result.append(path)
    return result


demo_runtime_dirs: dict[str, Path] = {}
demo_runtime_targets: dict[str, list[Target]] = {"mutatris": [mutatris_runtime_target]}
# Do not generate (or stage) any per-demo assets for a libraries-only build.
# Pcons otherwise registers their custom commands even though no executable
# consumes them, which both wastes work and can race with bundled .spv assets.
selected_example_dirs = (
    {directory for directory, unused, sources in EXAMPLES} if with_examples else set()
)

for demo_name in sorted(selected_example_dirs - {"mutatris"}):
    demo_source_dir = project_dir / "examples" / demo_name
    runtime_dir = (project_dir / project.build_dir / "runtime" / demo_name).resolve()
    demo_runtime_dirs[demo_name] = runtime_dir

    asset_roots = [
        directory
        for directory in (demo_source_dir / "data", demo_source_dir / "shaders")
        if directory.is_dir()
    ]
    asset_files = sorted(
        path for directory in asset_roots for path in directory.rglob("*") if path.is_file()
    )
    extra_asset_directories = [
        (project_dir / source, runtime_dir / destination)
        for source, destination in EXTRA_DEMO_RUNTIME_DIRECTORIES.get(demo_name, [])
    ]
    extra_asset_files = [
        (project_dir / source, runtime_dir / destination)
        for source, destination in EXTRA_DEMO_RUNTIME_FILES.get(demo_name, [])
    ]
    asset_files.extend(
        path
        for source, destination in extra_asset_directories
        for path in source.rglob("*")
        if path.is_file()
    )
    asset_files.extend(source for source, destination in extra_asset_files)
    asset_marker = runtime_dir / ".pcons-assets"
    asset_commands = [f"mkdir -p {quoted(runtime_dir)}"]
    for directory in asset_roots:
        destination = runtime_dir / directory.name
        asset_commands.extend(
            [
                f"mkdir -p {quoted(destination)}",
                f"cp -a {quoted(directory)}/. {quoted(destination)}/",
            ]
        )
    for source, destination in extra_asset_directories:
        asset_commands.extend(
            [
                f"mkdir -p {quoted(destination)}",
                f"cp -a {quoted(source)}/. {quoted(destination)}/",
            ]
        )
    for source, destination in extra_asset_files:
        asset_commands.extend(
            [
                f"mkdir -p {quoted(destination.parent)}",
                f"cp {quoted(source)} {quoted(destination)}",
            ]
        )
    asset_commands.append(f"touch {quoted(asset_marker)}")
    asset_target = project.Command(
        f"runtime-assets-{demo_name}",
        env,
        target=asset_marker,
        source=asset_files or [demo_source_dir / "CMakeLists.txt"],
        command=" && ".join(asset_commands),
    )

    core_marker = runtime_dir / "data" / ".pcons-core-assets"
    core_commands = [
        f"mkdir -p {quoted(runtime_dir / 'data')} {quoted(runtime_dir / 'shaders')}"
    ]
    core_aliases = (
        ("sprite.vert.spv", ("sprite.vert.spv", "sprite_vert.spv")),
        ("sprite.frag.spv", ("sprite.frag.spv", "sprite_frag.spv")),
        ("text.vert.spv", ("text.vert.spv", "text_vert.spv")),
        ("text.frag.spv", ("text.frag.spv", "text_frag.spv")),
        ("sprite3d.vert.spv", ("sprite3d.vert.spv",)),
        ("sprite3d.frag.spv", ("sprite3d.frag.spv",)),
    )
    for source_name, aliases in core_aliases:
        for alias in aliases:
            for destination in (
                runtime_dir / alias,
                runtime_dir / "data" / alias,
                runtime_dir / "shaders" / alias,
            ):
                core_commands.append(
                    f"cp {quoted(shader_output_dir / source_name)} "
                    f"{quoted(destination)}"
                )
    core_commands.extend(
        [
            f"cp {quoted(project_dir / 'mxvk' / 'data' / 'default.ttf')} "
            f"{quoted(runtime_dir / 'data' / 'default.ttf')}",
            f"touch {quoted(core_marker)}",
        ]
    )
    core_target = project.Command(
        f"runtime-core-{demo_name}",
        env,
        target=core_marker,
        source=[asset_marker, project_dir / "mxvk" / "data" / "default.ttf"]
        + [shader_output_dir / source_name for source_name, aliases in core_aliases],
        command=" && ".join(core_commands),
    )

    local_shader_sources = sorted(
        path
        for path in demo_source_dir.rglob("*")
        if path.suffix in (".vert", ".frag", ".comp", ".glsl")
        and path.relative_to(project_dir).as_posix() not in LEGACY_NON_VULKAN_SHADERS
    )
    shared_shader_sources = [
        project_dir / relative
        for relative in EXTRA_DEMO_SHADER_SOURCES.get(demo_name, [])
    ]
    runtime_shader_sources = unique_paths(local_shader_sources + shared_shader_sources)
    compile_targets = []
    compiled_outputs: list[tuple[Path, Path, str]] = []
    for shader_index, shader in enumerate(runtime_shader_sources):
        source_key = shader.relative_to(project_dir).as_posix()
        compiled = runtime_dir / ".compiled" / f"{shader_index}-{shader.name}.spv"
        flags = "-fshader-stage=frag" if shader.suffix == ".glsl" else ""
        compile_target = project.Command(
            f"runtime-shader-{demo_name}-{shader_index}",
            env,
            target=compiled,
            source=shader,
            command=(
                f"mkdir -p {quoted(compiled.parent)} && glslc {flags} "
                f"{quoted(shader)} -o {quoted(compiled)}"
            ),
        )
        compile_targets.append(compile_target)
        compiled_outputs.append((shader, compiled, source_key))

    shader_marker = runtime_dir / ".pcons-shaders"
    shader_commands = [f"mkdir -p {quoted(runtime_dir)}"]
    for shader, compiled, source_key in compiled_outputs:
        if shader.is_relative_to(demo_source_dir):
            relative_shader = shader.relative_to(demo_source_dir)
        else:
            relative_shader = Path("shared") / shader.name
        destinations = [
            runtime_dir / Path(f"{relative_shader}.spv"),
            runtime_dir / "data" / f"{shader.name}.spv",
            runtime_dir / "shaders" / f"{shader.name}.spv",
        ]
        for special_output in SPECIAL_SHADER_OUTPUTS.get(
            (demo_name, source_key), []
        ):
            special_path = runtime_dir / special_output
            destinations.extend(
                [
                    special_path,
                    runtime_dir / "data" / special_path.name,
                    runtime_dir / "shaders" / special_path.name,
                ]
            )
        for destination in unique_paths(destinations):
            shader_commands.extend(
                [
                    f"mkdir -p {quoted(destination.parent)}",
                    f"cp {quoted(compiled)} {quoted(destination)}",
                ]
            )
    shader_commands.append(f"touch {quoted(shader_marker)}")
    shader_target = project.Command(
        f"runtime-shaders-{demo_name}",
        env,
        target=shader_marker,
        source=[asset_marker, core_marker]
        + [compiled for shader, compiled, source_key in compiled_outputs],
        command=" && ".join(shader_commands),
    )
    demo_runtime_targets[demo_name] = [shader_target]

demo_runtime_dirs["mutatris"] = mutatris_runtime_dir

programs: list[tuple[str, Target]] = []
if with_examples:
    rain = project.StaticLibrary(
        "rain", env, sources=[project_dir / "examples" / "rain" / "rain.cpp"]
    )
    rain.public.include_dirs.append(project_dir / "examples" / "rain")
    rain.link(mxvk)

    rain_users = {"model_example", "planet", "asteroids3d", "asteroids-net", "defender", "puzzle_drop", "matrix"}
    network_users = {"asteroids-net", "tetris"}
    math_examples = {name for directory, name, unused in EXAMPLES if directory.startswith("3dmath")}
    for directory, name, source_names in EXAMPLES:
        source_dir = project_dir / "examples" / directory
        program = project.Program(name, env, sources=[source_dir / item for item in source_names])
        program.link(rain if directory in rain_users else mxvk)
        if directory in network_users:
            program.link(mxnetwork)
        if directory in ("asteroids3d", "asteroids-net"):
            program.private.include_dirs.append(project_dir / "examples" / "rain")
            program.private.defines.extend(
                [
                    f'ASTEROIDS3D_SOURCE_DATA_DIR="{source_dir / "data"}"',
                    f'ASTEROIDS3D_DEFENDER_SOUND_DIR="{project_dir / "examples" / "defender" / "data"}"',
                ]
            )
        if directory == "defender":
            program.private.include_dirs.extend(
                [project_dir / "examples" / "rain", project_dir / "examples" / "asteroids3d"]
            )
        if name in math_examples and with_eigen and eigen:
            program.private.defines.append("MXVK_USE_EIGEN_MATH")
            program.link(eigen)
        if directory == "3dmath_obj_loader":
            program.private.defines.append("MXVK_OBJ_LOADER")
        program.add_dependency(*demo_runtime_targets.get(directory, []))
        if directory == "asteroids-net":
            if (Path("/usr/include/miniupnpc/miniupnpc.h")).exists():
                program.private.defines.append("ASTEROIDS_NET_HAS_MINIUPNPC=1")
                program.private.link_libs.append("miniupnpc")
            if (Path("/usr/include/natpmp.h")).exists():
                program.private.defines.append("ASTEROIDS_NET_HAS_NATPMP=1")
                program.private.link_libs.append("natpmp")
        asset_dir = demo_runtime_dirs.get(directory, source_dir)
        if directory in asset_defines:
            program.private.defines.append(f'{asset_defines[directory]}="{asset_dir}"')
        if directory in ("sprite_example", "text_example"):
            program.private.defines.append(f'{directory}_SHADER_DIR="{source_dir / "shaders"}"')
        if directory == "viewer":
            program.private.defines.append(f'VIEWER_SOURCE_DIR="{source_dir}"')
        if directory == "starship":
            program.private.defines.append(
                f'STARSHIP_EXAMPLE_RUNTIME_DATA_DIR="{source_dir / "data"}"'
            )
        if directory == "shader_viewer":
            program.private.defines.append(f'shader_viewer_SOURCE_DIR="{source_dir}"')
        if directory == "tictactoe":
            program.private.defines.append(f'tictactoe_FONT_PATH="{source_dir / "data" / "font.ttf"}"')
        if with_cv and directory in ("compute_shader", "opencv_example", "shader_viewer", "opencv_model") and opencv:
            program.link(opencv)
        programs.append((name, program))

stage_prefix = get_var("PCONS_INSTALL_PREFIX", str(project_dir / "dist"))
final_prefix = get_var("PCONS_FINAL_PREFIX", stage_prefix)
pc_file = project.build_dir / "mxvk.pc"
pc_file.parent.mkdir(parents=True, exist_ok=True)
requires = "sdl3 vulkan libpng zlib"
private_requires = " ".join(
    (["libavcodec", "libavformat", "libavutil", "libswscale"] if with_mxwrite else [])
    + ([get_var("OPENCV_PACKAGE", "opencv5")] if opencv else [])
)
private_libraries = ["-ldl", "-pthread"] if platform.is_linux else []
if with_mixer:
    private_libraries.append("-lSDL3_mixer")
if with_jpeg:
    private_libraries.append("-ljpeg")
if with_cuda and cuda:
    private_libraries.extend(
        [f"-L{cuda_lib.parent}", "-lcudart", "-lnppicc", "-lnppidei", "-lnppc"]
    )
public_defines = []
if platform.is_macos:
    # MXVK's public headers and consumers must use the same Vulkan platform
    # declarations as the library.  This mirrors CMake's PUBLIC definitions.
    public_defines.extend(["-DMXVK_USE_MOLTENVK", "-DVK_ENABLE_BETA_EXTENSIONS"])
if with_mixer:
    public_defines.extend(["-DMXVK_WITH_MIXER", "-DWITH_MIXER"])
if with_jpeg:
    public_defines.extend(["-DMXVK_WITH_JPEG", "-DWITH_JPEG"])
if with_cv:
    public_defines.append("-DMXVK_WITH_CV")
if with_cuda:
    public_defines.extend(["-DMXVK_CUDA", "-DMXVK_CUDA_NPP"])
if with_mxwrite:
    public_defines.extend(["-DMXVK_WITH_FFMPEG_CAPTURE", "-DMXWRITE_ENABLED=1"])
    if with_cuda:
        public_defines.append("-DMXWRITE_HAS_CUDA_COPY=1")
pc_file.write_text(
    f"prefix={final_prefix}\n"
    "exec_prefix=${prefix}\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\n\n"
    "Name: mxvk\nDescription: C++20 SDL3 Vulkan rendering library\n"
    f"Version: {VERSION}\nRequires: {requires}\n"
    f"Requires.private: {private_requires}\n"
    "Libs: -L${libdir} -lmxvk -lvolk -lSDL3_ttf\n"
    f"Libs.private: {' '.join(private_libraries)}\n"
    f"Cflags: -I${{includedir}} {' '.join(public_defines)}\n"
)

public_headers = sorted((project_dir / "mxvk" / "include" / "mxvk").glob("*.h*"))
network_headers = sorted((project_dir / "MXNetwork" / "include" / "mxnetwork").glob("*.h*"))
installed: list[Target] = [
    project.Install("include/mxvk", public_headers + [version_h]),
    project.Install("include/mxnetwork", network_headers),
    project.Install("include", [project_dir / "MXWrite" / "mxwrite.hpp"]) if with_mxwrite else [],
    project.Install("include/volk", [project_dir / "volk" / "volk.h"]),
    project.Install("lib", [mxvk, volk, mxnetwork] + ([mxwrite] if mxwrite else [])),
    project.Install("lib/pkgconfig", [pc_file]),
    project.Install("bin", [mxmod2obj]),
]
installed = [target for target in installed if target]
if with_examples:
    installed.extend(
        project.Install(f"libexec/mxvk/{name}", [program])
        for name, program in programs
    )
project.Alias("install", *installed)
