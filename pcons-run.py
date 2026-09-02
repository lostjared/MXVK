#!/usr/bin/env python3
"""Run MXVK examples produced by the pcons build.

Unlike run.pl, which expects CMake's build/examples/<name>/<executable>
layout, this launcher uses pcons' flat build/pcons/<executable> layout.
"""

from __future__ import annotations

import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent
BUILD_DIR = Path(
    os.environ.get("MXVK_PCONS_BUILD_DIR", str(ROOT_DIR / "build" / "pcons"))
).expanduser()
SOURCE_DIR = ROOT_DIR / "examples"
MISSING_EXECUTABLE_EXIT_CODE = 3
DEFAULT_TIMEOUT_SECONDS = 5.0

TEST_PROGRAMS = (
    "hello_world",
    "text_example",
    "sprite_example",
    "sprite3d_example",
    "static_example",
    "surface",
    "stencil",
    "stencil_surface",
    "pointsprite",
    "knight",
    "3dmath",
    "3dmath_cube",
    "3dmath_masterpiece",
    "fire",
    "dark",
    "matrix",
    "binary_matrix",
    "planet",
    "masterpiece",
    "mutatris",
    "console_demo",
    "postprocess",
    "fractal_zoom",
    "model_example",
    "starship",
    "moon",
    "breakout",
    "asteroids",
    "asteroids3d",
    "defender",
    "glitch_cube",
    "tictactoe",
    "pong",
    "pool_demo",
    "puzzle",
    "tetris",
    "puzzle_drop",
    "tux_example",
    "walk",
    "fireworks",
    "bluesky",
    "asteroids-net",
)


def resolve_executable_name(program_name: str) -> str | None:
    """Read the CMake target/output name without depending on CMake itself."""
    cmake_file = SOURCE_DIR / program_name / "CMakeLists.txt"
    if not cmake_file.is_file():
        return None

    cmake = cmake_file.read_text(encoding="utf-8")
    target_match = re.search(r"add_executable\s*\(\s*([^\s)]+)", cmake, re.DOTALL)
    if not target_match:
        return None

    target_name = target_match.group(1)
    properties_match = re.search(
        rf"set_target_properties\s*\(\s*{re.escape(target_name)}\s+PROPERTIES\s+([^)]*)\)",
        cmake,
        re.DOTALL,
    )
    if properties_match:
        output_match = re.search(
            r'OUTPUT_NAME\s+"([^"]+)"', properties_match.group(1), re.DOTALL
        )
        if output_match:
            return output_match.group(1)
    return target_name


def executable_path(program_name: str) -> Path | None:
    executable_name = resolve_executable_name(program_name)
    return BUILD_DIR / executable_name if executable_name else None


def available_programs() -> list[str]:
    programs = []
    if not SOURCE_DIR.is_dir():
        return programs
    for source_path in SOURCE_DIR.iterdir():
        if not source_path.is_dir():
            continue
        executable = executable_path(source_path.name)
        if executable and executable.is_file() and os.access(executable, os.X_OK):
            programs.append(source_path.name)
    return sorted(programs)


def normalize_file_arguments(arguments: list[str], invocation_dir: Path) -> list[str]:
    normalized = list(arguments)
    index = 0
    while index < len(normalized):
        argument = normalized[index]
        if argument in ("--filename", "--model") and index + 1 < len(normalized):
            value = Path(normalized[index + 1])
            if not value.is_absolute():
                normalized[index + 1] = str((invocation_dir / value).resolve())
            index += 2
            continue
        match = re.match(r"^(--filename|--model)=(.+)$", argument)
        if match:
            value = Path(match.group(2))
            if not value.is_absolute():
                value = (invocation_dir / value).resolve()
            normalized[index] = f"{match.group(1)}={value}"
        index += 1
    return normalized


def terminate_process_group(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=2)
    except (ProcessLookupError, subprocess.TimeoutExpired):
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()


def run_program(
    program_name: str,
    arguments: list[str],
    timeout_seconds: float | None,
    *,
    debug: bool = False,
) -> int:
    source_path = SOURCE_DIR / program_name
    cmake_file = source_path / "CMakeLists.txt"
    if not cmake_file.is_file():
        print(
            f"Error: could not find source CMake file for {program_name!r} at {cmake_file}",
            file=sys.stderr,
        )
        return MISSING_EXECUTABLE_EXIT_CODE

    executable = executable_path(program_name)
    if not executable or not executable.is_file() or not os.access(executable, os.X_OK):
        print(
            f"Skipping {program_name!r}: could not find pcons executable at {executable}",
            file=sys.stderr,
        )
        return MISSING_EXECUTABLE_EXIT_CODE

    pcons_runtime_path = BUILD_DIR / "runtime" / program_name
    runtime_marker = pcons_runtime_path / ".pcons-shaders"
    if not runtime_marker.is_file():
        print(
            f"Error: pcons runtime shaders are incomplete for {program_name!r}; "
            "rebuild the example with pcons",
            file=sys.stderr,
        )
        return MISSING_EXECUTABLE_EXIT_CODE
    runtime_path = pcons_runtime_path
    command = [str(executable), "-p", str(runtime_path), *arguments]
    if debug:
        if not shutil.which("gdb"):
            print("Error: gdb was not found", file=sys.stderr)
            return 127
        command = ["gdb", "-q", "-ex", "set confirm off", "-ex", "run", "--args", *command]

    print(f">> Executing: {shlex.join(command)}", flush=True)
    environment = os.environ.copy()
    environment.setdefault("MXVK_QUIET_MISSING_VALIDATION", "1")
    process = subprocess.Popen(
        command,
        cwd=runtime_path,
        env=environment,
        start_new_session=True,
    )
    try:
        return process.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired:
        print(
            f">> Timeout reached for {program_name} after {timeout_seconds:g}s; closing as requested",
            flush=True,
        )
        terminate_process_group(process)
        return 0
    except KeyboardInterrupt:
        terminate_process_group(process)
        return 130


def parse_arguments(
    arguments: list[str], invocation_dir: Path
) -> tuple[str | None, list[str], float | None, bool, Path]:
    program = None
    forwarded = []
    timeout_seconds = None
    debug = False
    build_dir = BUILD_DIR
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if program is None and argument == "--debug":
            debug = True
            index += 1
            continue
        if argument == "--build-dir":
            if index + 1 >= len(arguments):
                raise ValueError("--build-dir requires a directory")
            build_dir = Path(arguments[index + 1]).expanduser()
            if not build_dir.is_absolute():
                build_dir = invocation_dir / build_dir
            build_dir = build_dir.resolve()
            index += 2
            continue
        build_dir_match = re.fullmatch(r"--build-dir=(.+)", argument)
        if build_dir_match:
            build_dir = Path(build_dir_match.group(1)).expanduser()
            if not build_dir.is_absolute():
                build_dir = invocation_dir / build_dir
            build_dir = build_dir.resolve()
            index += 1
            continue
        if argument == "--timeout":
            timeout_seconds = DEFAULT_TIMEOUT_SECONDS
            if index + 1 < len(arguments) and re.fullmatch(
                r"\d+(?:\.\d+)?", arguments[index + 1]
            ):
                timeout_seconds = float(arguments[index + 1])
                index += 1
            index += 1
            continue
        timeout_match = re.fullmatch(r"--timeout=(\d+(?:\.\d+)?)", argument)
        if timeout_match:
            timeout_seconds = float(timeout_match.group(1))
            index += 1
            continue
        if program is None:
            program = argument
        else:
            forwarded.append(argument)
        index += 1

    if timeout_seconds is None and os.environ.get("CODEX_CI") and not sys.stdout.isatty():
        timeout_seconds = float(
            os.environ.get("MXVK_RUN_DEFAULT_TIMEOUT", DEFAULT_TIMEOUT_SECONDS)
        )
    return program, forwarded, timeout_seconds, debug, build_dir


def show_usage() -> int:
    print("Usage: ./pcons-run.py <program_name> [extra args...]")
    print("       ./pcons-run.py --build-dir <dir> <program_name> [extra args...]")
    print("       ./pcons-run.py <program_name> --timeout[=seconds] [extra args...]")
    print("       ./pcons-run.py --debug <program_name> [extra args...]")
    print("       ./pcons-run.py --all --timeout[=seconds] [extra args...]\n")
    print("Available pcons programs:")
    programs = available_programs()
    for program in programs:
        print(f"  {program}")
    print(f"{len(programs)} total program(s)")
    return 0 if programs else 1


def run_all(
    forwarded: list[str], timeout_seconds: float | None, debug: bool
) -> int:
    successful = 0
    skipped = 0
    failures = []
    programs = list(dict.fromkeys((*TEST_PROGRAMS, *available_programs())))
    for program in programs:
        result = run_program(program, forwarded, timeout_seconds, debug=debug)
        if result == MISSING_EXECUTABLE_EXIT_CODE:
            skipped += 1
        elif result == 0:
            successful += 1
        elif result == 130:
            return result
        else:
            failures.append(f"{program}: exit code {result}")

    print(
        f"{successful} program(s) ran successfully, {skipped} skipped, "
        f"{len(failures)} failure(s)."
    )
    for failure in failures:
        print(f"  - {failure}")
    return 1 if failures else 0


def main() -> int:
    global BUILD_DIR
    try:
        program, forwarded, timeout_seconds, debug, build_dir = parse_arguments(
            sys.argv[1:], Path.cwd()
        )
    except ValueError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 2
    BUILD_DIR = build_dir
    if program in (None, "-h", "--help", "--list"):
        return show_usage()

    forwarded = normalize_file_arguments(forwarded, Path.cwd())
    if program == "--all":
        return run_all(forwarded, timeout_seconds, debug)
    return run_program(Path(program).name, forwarded, timeout_seconds, debug=debug)


if __name__ == "__main__":
    raise SystemExit(main())
