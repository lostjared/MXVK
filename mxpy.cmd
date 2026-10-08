@echo off
setlocal
if defined MXVK_PYTHON (
    "%MXVK_PYTHON%" "%~dp0python-examples\mxpy.py" %*
) else (
    python.exe --version >nul 2>nul
    if errorlevel 1 (
        py "%~dp0python-examples\mxpy.py" %*
    ) else (
        python.exe "%~dp0python-examples\mxpy.py" %*
    )
)
exit /b %errorlevel%
