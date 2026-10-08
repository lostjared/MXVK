param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$Python = $env:MXVK_PYTHON,
    [string]$BuildDir = "build-python-windows",
    [string]$Triplet = "x64-windows",
    [switch]$WithOpenCV
)

$ErrorActionPreference = "Stop"
$repo_dir = $PSScriptRoot

if (-not $VcpkgRoot -and (Test-Path -LiteralPath "C:\vcpkg\scripts\buildsystems\vcpkg.cmake")) {
    $VcpkgRoot = "C:\vcpkg"
}
if (-not $VcpkgRoot) {
    throw "Set VCPKG_ROOT or pass -VcpkgRoot to your vcpkg checkout. See python-examples/README.md."
}
$VcpkgRoot = (Resolve-Path -LiteralPath $VcpkgRoot).Path
$toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path -LiteralPath $toolchain)) {
    throw "vcpkg toolchain not found: $toolchain"
}
if (-not $Python) {
    $vcpkg_python = Join-Path $VcpkgRoot "installed\$Triplet\tools\python3\python.exe"
    if (Test-Path -LiteralPath $vcpkg_python) {
        $Python = $vcpkg_python
    } else {
        $Python = (Get-Command python.exe -ErrorAction Stop).Source
    }
}
$Python = (Get-Command $Python -ErrorAction Stop).Source
& $Python -c "import nanobind, numpy"
if ($LASTEXITCODE -ne 0) {
    throw "Install Python prerequisites first: & '$Python' -m pip install nanobind numpy"
}
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $repo_dir $BuildDir
}
$cv = if ($WithOpenCV) { "ON" } else { "OFF" }
Write-Host "Building MXVK for $Python in $BuildDir"
& cmake -S $repo_dir -B $BuildDir `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DVCPKG_TARGET_TRIPLET=$Triplet" `
    "-DPython_EXECUTABLE=$Python" -DPYTHON_MODULE=ON -DEXAMPLES=OFF `
    -DWITH_CUDA=OFF -DWITH_MXWRITE=OFF -DWITH_MIXER=OFF "-DCV=$cv" -DJPEG=OFF
if ($LASTEXITCODE -ne 0) { throw "MXVK CMake configuration failed." }
& cmake --build $BuildDir --config Release --target mxvk_ext --parallel
if ($LASTEXITCODE -ne 0) { throw "MXVK Python build failed." }
$module_dir = $BuildDir
if (Test-Path -LiteralPath (Join-Path $BuildDir "Release")) {
    $module_dir = Join-Path $BuildDir "Release"
}
& $Python (Join-Path $repo_dir "python-examples\mxpy.py") --module-dir $module_dir --check
if ($LASTEXITCODE -ne 0) { throw "MXVK import check failed. See python-examples/README.md." }
Write-Host "Build complete. Run .\mxpy.cmd --module-dir `"$module_dir`" sprite --vsync"
