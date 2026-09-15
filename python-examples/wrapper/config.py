## @file config.py
## @brief Friendly persistent settings wrapper.

from pathlib import Path

from ._native import mxvk


class Settings:
    ## @brief A small INI-style MXVK settings file.

    def __init__(self, path: str | Path | None = None) -> None:
        ## @brief Create empty settings or load them from @p path.
        self.native = mxvk.Config() if path is None else mxvk.Config(str(path))

    def get(self, section: str, key: str, default: str = "") -> str:
        ## @brief Return a setting, falling back to @p default when missing.
        return self.native.item_at_key(section, key, default).value

    def set(self, section: str, key: str, value: str) -> None:
        ## @brief Store a string setting.
        self.native.set_item(section, key, value)

    def load(self, path: str | Path) -> None:
        ## @brief Replace settings with the contents of @p path.
        self.native.load_file(str(path))

    def save(self, path: str | Path) -> None:
        ## @brief Write settings to @p path.
        self.native.save_file(str(path))
