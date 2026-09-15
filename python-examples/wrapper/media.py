## @file media.py
## @brief Optional media and timing wrappers.

from pathlib import Path

from ._native import mxvk


class Stopwatch:
    ## @brief A straightforward elapsed-time stopwatch.

    def __init__(self, name: str = "app") -> None:
        ## @brief Create and start a steady-clock stopwatch named @p name.
        self.native = mxvk.SteadyStopwatch(name)
        self.native.start(name)

    def seconds(self) -> float:
        ## @brief Return elapsed seconds since the latest start.
        return self.native.time_passed() / 1000.0

    def restart(self, name: str = "app") -> None:
        ## @brief Restart measurement with an optional label.
        self.native.start(name)


class Sound:
    ## @brief Optional WAV and music playback helper.

    def __init__(self) -> None:
        ## @brief Initialize the audio mixer.
        if not getattr(mxvk, "has_mixer", False):
            raise RuntimeError("MXVK was built without MIXER support")
        self.native = mxvk.Mixer()
        self.native.init()

    def load_wav(self, path: str | Path) -> int:
        ## @brief Load a WAV file and return its playback ID.
        return self.native.load_wav(str(path))

    def play(self, sound_id: int, loops: int = 0) -> int:
        ## @brief Play a loaded WAV and return its mixer channel.
        return self.native.play_wav(sound_id, loops)

    def close(self) -> None:
        ## @brief Release mixer resources.
        self.native.cleanup()


class Camera:
    ## @brief Optional OpenCV camera or video reader.

    def __init__(self, source: int | str | Path = 0) -> None:
        ## @brief Open camera index or video-file @p source.
        if not getattr(mxvk, "has_cv", False):
            raise RuntimeError("MXVK was built without CV support")
        self.native = mxvk.Capture()
        if isinstance(source, int):
            self.native.open_camera(source)
        else:
            self.native.open(str(source))

    def frame(self, *, flip_y: bool = False) -> object | None:
        ## @brief Read the next RGBA NumPy frame, or @c None at end of stream.
        return self.native.read_rgba(flip_y)

    def close(self) -> None:
        ## @brief Close the camera or video file.
        self.native.close()
