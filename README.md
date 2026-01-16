# Xosview Clone for macOS (OSXview)

A C++ clone of the classic Xosview system monitor for macOS, built with SDL2.

## Features

- Real-time CPU usage monitoring
- Memory display
- Network I/O graphs (in/out)
- Disk I/O graphs (read/write)

## Demo

<video width="100%" controls>
  <source src="https://raw.githubusercontent.com/masikh/OSXView/main/OSXview.mp4" type="video/mp4">
</video>

## Dependencies

- SDL2
- CMake 3.16 or higher
- Xcode Command Line Tools (for macOS system libraries)

## Installation

1. Install SDL2 using Homebrew:
```bash
brew install sdl2
```

2. Build the project:
```bash
cmake --build cmake-build-debug --target OSXview
cmake --build cmake-build-debug --target bundle_script
```

3. Run the application:
```bash
./OSXview.app/Contents/MacOS/OSXview (or click ./OSXview.app)
```
