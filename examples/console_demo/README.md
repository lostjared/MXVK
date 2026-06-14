# Console Demo

This sample shows the MXVK in-window console layered over a moving shader background. It is mainly a demonstration of how the engine can combine regular rendering with an interactive text console.

## Controls

- `F3` - open or close the console
- `Escape` - quit when the console is hidden
- `help` - show built-in console commands
- `echo ...` - print text back into the console
- `about` - print the sample banner
- `quit` or `exit` - close the window from the console

## How It Works

The background is a full-screen sprite driven by a time-based fragment shader. The console is attached to the window, receives SDL events directly, and invokes a command callback for custom commands while still exposing the built-in console command set.

