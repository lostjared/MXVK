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

VERSION = "0.24.0"
project_dir = Path(__file__).parent.resolve()
platform = get_platform()


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
search_prefixes = extra_prefixes + [Path("/usr/local"), Path("/usr"), Path("/opt/cuda")]

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
        "PROJECT_VERSION_MINOR": "24",
        "PROJECT_VERSION_PATCH": "0",
    },
)

shader_targets: list[Target] = []
shader_output_dir = (project_dir / project.build_dir / "mxvk" / "shaders").resolve()
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
        f'MXVK_DEFAULT_FONT_DIR="{project_dir / "mxvk" / "data"}"',
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
mxvk.add_dependency(*shader_targets)

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
        if directory == "asteroids-net":
            if (Path("/usr/include/miniupnpc/miniupnpc.h")).exists():
                program.private.defines.append("ASTEROIDS_NET_HAS_MINIUPNPC=1")
                program.private.link_libs.append("miniupnpc")
            if (Path("/usr/include/natpmp.h")).exists():
                program.private.defines.append("ASTEROIDS_NET_HAS_NATPMP=1")
                program.private.link_libs.append("natpmp")
        asset_dir = source_dir
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
