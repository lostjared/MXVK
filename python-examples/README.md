# Running Python examples on Windows

From PowerShell in the repository root:

```powershell
.\mxpy.cmd --list
.\mxpy.cmd --check
.\mxpy.cmd window
.\mxpy.cmd sprite --width 1280 --height 720 --vsync
.\mxpy.cmd tetris
```

The launcher finds `mxvk_ext` in `python_mod` and `build*` directories, including
Visual Studio's `Release`, `RelWithDebInfo`, and `Debug` subdirectories. It adds
the example wrapper package and native DLL directories automatically. No manual
`PYTHONPATH` setup is needed. It also supports a module installed with pip.

If your default Python does not match the extension, the launcher looks for a
compatible interpreter in CMake build metadata, vcpkg, and the Windows Python
launcher's installed interpreters. For example, a `cp313` extension requires
Python 3.13 with the matching architecture; Python 3.14 cannot load it.
The selected interpreter is printed when the launcher switches Python.

Run your own script or import the module directly through the same environment:

```powershell
.\mxpy.cmd "C:\my project\demo.py" --input "C:\videos\clip.mp4"
.\mxpy.cmd -c "import mxvk_ext; print(mxvk_ext.__file__)"
.\mxpy.cmd -m mxvk_wrap.examples.tetris.main
```

Paths passed to your program remain relative to your current working directory.
You can invoke `C:\path\to\MXVK\mxpy.cmd` from any directory. The original
`python python-examples/mxpy.py script.py` and `run_mxvk.py` entry points also
work, including on Linux and macOS (Windows DLL setup is skipped there).

## Build the extension

Install Visual Studio's C++ build tools, CMake, the Vulkan SDK 1.4 or newer,
and vcpkg. Run the build from a Visual Studio Developer PowerShell. With an
existing vcpkg checkout:

```powershell
$env:VCPKG_ROOT = "C:\vcpkg"
& "$env:VCPKG_ROOT\vcpkg.exe" install sdl3:x64-windows sdl3-ttf:x64-windows libpng:x64-windows zlib:x64-windows glm:x64-windows
```

Choose the Python that will build and run the extension. When vcpkg has installed
its own Python, use that interpreter so its headers and libraries agree:

```powershell
$python = "$env:VCPKG_ROOT\installed\x64-windows\tools\python3\python.exe"
# Otherwise, use your installed 64-bit Python:
# $python = (Get-Command python.exe).Source
& $python -m pip install nanobind numpy
.\build-python.ps1 -Python $python
```

The helper configures a separate `build-python-windows` directory, builds only
the extension and its dependencies in Release, and verifies the native import.
It uses `VCPKG_ROOT` (or an existing `C:\vcpkg`) and prefers vcpkg's Python when
`-Python` is omitted. It does not install dependencies. You can pass
`-VcpkgRoot`, `-BuildDir`, or `-Triplet` for another setup. If PowerShell blocks
local scripts, invoke it for this process with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-python.ps1 -Python $python
```

CUDA, audio mixer, MXWrite, and OpenCV support are disabled in this basic build.
For the capture API, install `opencv4:x64-windows` with vcpkg and add
`-WithOpenCV`. The `opencv_mxwrite` and `opencv_mxwrite_shader` examples also need
the separate `mxwrite_ext` extension and `opencv-python` installed into the
selected interpreter. See the repository README's Python wheel instructions
for feature configuration; the helper builds only MXVK.

## Select a build or supply DLL directories

An extension matching the current Python takes priority. Otherwise, a matching
interpreter is selected for a discovered build. To choose a specific build,
put launcher options before the example name:

```powershell
.\mxpy.cmd --module-dir .\build-python-windows\Release --check
.\mxpy.cmd --module-dir .\build-python-windows\Release sprite --vsync
.\mxpy.cmd --dll-dir "D:\dependencies\bin" model
```

The module directory must contain the `.pyd` itself. You can also set
`MXVK_PYTHON_MODULE_DIR`. DLLs beside the extension, vcpkg's triplet `bin`,
`CUDA_PATH\bin`, `CUDA_PATH\bin\x64`, `VULKAN_SDK\bin`, and existing `PATH`
directories are registered with Python. `VCPKG_ROOT` identifies a custom
vcpkg checkout; `VCPKG_DEFAULT_TRIPLET` chooses its triplet (default:
`x64-windows`). `MXVK_DLL_DIRS` accepts additional directories separated by
semicolons, or repeat `--dll-dir` for each directory.

`MXVK_PYTHON` selects the Python executable used to start `mxpy.cmd`. To use a
virtual environment, activate it before running the launcher and select a build
compiled for that Python version. A version mismatch can cause the launcher to
switch interpreters; packages in the original environment will then be unavailable.

`--check` imports MXVK without opening a window and reports availability of
NumPy, OpenCV's `cv2`, and MXWrite. Missing optional packages do not fail this
check. For a DLL load error, verify dependency directories and matching 64-bit
libraries. For an incompatible Python error, rebuild with the intended Python
or run with the interpreter used for the existing build.
