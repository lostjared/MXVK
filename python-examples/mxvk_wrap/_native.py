## @file _native.py
## @brief Imports the optional native MXVK extension with an actionable error.

try:
    import mxvk_ext as mxvk
except ImportError as error:
    raise ImportError(
        "MXVK's Python extension is unavailable. Build with "
        "-DPYTHON_MODULE=ON, then add its build directory to PYTHONPATH."
    ) from error

__all__ = ["mxvk"]
