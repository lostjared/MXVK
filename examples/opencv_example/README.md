# OpenCV Example

This sample shows how to stream a live OpenCV capture source into an MXVK sprite. It can read from a camera index or a video file, then resizes the output to the current swapchain and overlays the measured frame rate.

## Controls

- `Escape` - quit

## How It Works

The capture path opens either a camera or a file based on the shared argument parser, converts each frame to RGBA, and uploads it to a sprite. If the source is a file and reaches the end, the example reopens it so playback continues.

