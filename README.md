# Xosview Clone for macOS (OSXview)

A C++ clone of the classic Xosview system monitor for macOS, built with SDL2.

## Features

- Real-time CPU usage monitoring
- Memory display
- Network I/O graphs (in/out)
- Disk I/O graphs (read/write)
- Laptop battery charge, AC/charging status, and time remaining

## Demo

[![Watch the demo](https://raw.githubusercontent.com/masikh/OSXView/main/OSXview.png)](https://raw.githubusercontent.com/masikh/OSXView/main/OSXview.mp4)

## Dependencies

- SDL2
- CMake 3.16 or higher
- Xcode Command Line Tools (for macOS system libraries)

## Installation

1a. Build the project from source:
```bash
./build.sh
```

1b. unzip the pre-compiled bundle:
```bash
unzip ./OSXView.app.zip -d ./OSXView.app 
```

3. Run the application:
```bash
./OSXview.app/Contents/MacOS/OSXview (or click ./OSXview.app)
```
