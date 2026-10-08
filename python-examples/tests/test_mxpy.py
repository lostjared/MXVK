"""Launcher regression checks; run with python -m unittest discover -s python-examples/tests."""

import contextlib
import importlib.machinery
import importlib.util
import io
import os
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

LAUNCHER = Path(__file__).resolve().parents[1] / "mxpy.py"
SPEC = importlib.util.spec_from_file_location("mxpy", LAUNCHER)
mxpy = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mxpy)


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mxvk launcher ")
        self.directory = Path(self.temp.name).resolve()
        self.addCleanup(self.temp.cleanup)

    def test_example_aliases_exist(self):
        for filename in mxpy.EXAMPLES.values():
            self.assertTrue((mxpy.EXAMPLES_DIR / filename).is_file(), filename)

    def test_explicit_directory_overrides_environment(self):
        with patch.dict(os.environ, {"MXVK_PYTHON_MODULE_DIR": "another build"}):
            self.assertEqual(mxpy.module_directories(self.directory), [self.directory])

    def test_cache_in_configuration_parent(self):
        (self.directory / "CMakeCache.txt").write_text(
            "//ignored\nPython_EXECUTABLE:FILEPATH=C:/space here/python.exe\nCUSTOM:STRING=a=b\n",
            encoding="utf-8",
        )
        values = mxpy.read_cache(self.directory / "Release")
        self.assertEqual(values["Python_EXECUTABLE"], "C:/space here/python.exe")
        self.assertEqual(values["CUSTOM"], "a=b")

    def test_current_interpreter_wins(self):
        (self.directory / ("mxvk_ext" + importlib.machinery.EXTENSION_SUFFIXES[0])).touch()
        with patch.object(mxpy, "interpreter_candidates") as candidates:
            self.assertEqual(mxpy.select_module([self.directory]), (self.directory, None))
            candidates.assert_not_called()

    def test_incompatible_extension_is_rejected(self):
        (self.directory / "mxvk_ext.cp99999-win_amd64.pyd").touch()
        with patch.dict(os.environ, {"MXVK_LAUNCHER_REEXEC": "1"}):
            self.assertEqual(mxpy.select_module([self.directory]), (None, None))

    def test_missing_explicit_module_reports_directory(self):
        output = io.StringIO()
        with contextlib.redirect_stderr(output):
            self.assertEqual(mxpy.main(["--module-dir", str(self.directory), "--check"]), 1)
        self.assertIn("No MXVK extension found", output.getvalue())
        self.assertIn(str(self.directory), output.getvalue())

    def run_mocked(self, argv):
        native = types.ModuleType("mxvk_ext")
        native.__file__ = "mock native extension"
        handle = unittest.mock.Mock()
        old_path, old_argv = sys.path[:], sys.argv[:]
        self.addCleanup(setattr, sys, "path", old_path)
        self.addCleanup(setattr, sys, "argv", old_argv)
        with patch.dict(sys.modules, {"mxvk_ext": native}), \
                patch.object(mxpy, "select_module", return_value=(None, None)), \
                patch.object(mxpy, "has_module", return_value=False), \
                patch.object(mxpy, "configure_runtime", return_value=[handle]), \
                patch.dict(os.environ, {"MXVK_PYTHON_MODULE_DIR": ""}):
            result = mxpy.main(argv)
        handle.close.assert_called_once()
        return result

    def test_script_arguments_and_sibling_imports(self):
        (self.directory / "sibling.py").write_text("VALUE = 42\n", encoding="utf-8")
        script = self.directory / "my script.py"
        script.write_text(
            "import sys, sibling\nassert sibling.VALUE == 42\n"
            "assert sys.argv[1:] == ['--input', 'two words']\n"
            "assert __name__ == '__main__'\n",
            encoding="utf-8",
        )
        try:
            self.assertEqual(self.run_mocked([str(script), "--input", "two words"]), 0)
        finally:
            sys.modules.pop("sibling", None)

    def test_code_arguments(self):
        self.assertEqual(self.run_mocked([
            "-c", "import sys; assert sys.argv == ['-c', '--flag', 'two words']",
            "--flag", "two words",
        ]), 0)

    def test_module_arguments(self):
        with patch.object(mxpy.runpy, "run_module") as run_module:
            self.assertEqual(self.run_mocked(["-m", "example.module", "--flag", "two words"]), 0)
            run_module.assert_called_once_with("example.module", run_name="__main__", alter_sys=True)
            self.assertEqual(sys.argv, ["example.module", "--flag", "two words"])

    def test_list_needs_no_native_extension(self):
        with contextlib.redirect_stdout(io.StringIO()), patch.object(mxpy, "select_module") as select:
            self.assertEqual(mxpy.main(["--list"]), 0)
            select.assert_not_called()


if __name__ == "__main__":
    unittest.main()
