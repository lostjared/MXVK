# Config Example

Config Example is a tiny smoke test for `mxvk::VK_Config`. It reads a persisted value from `test.dat`, increments it, prints the new count, and writes it back.

## Inputs

- No command-line arguments are required.
- The example reads and writes `test.dat` in the current working directory.

## Controls

- None. The program runs once and exits.

## How It Works

The sample opens a config file, looks up `global.key`, converts the stored string to an integer, increments it, and saves the updated value. It is a quick way to verify that config persistence works in the current environment.
