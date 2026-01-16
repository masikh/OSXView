# OSXview Clone for macOS

A C++ clone of the classic OSXview system monitor for macOS, built with SDL2.

## Features

- Real-time CPU usage monitoring
- Memory display
- Network I/O graphs (in/out)
- Disk I/O graphs (read/write)

## Demo

![OSXview Demo](OSXview.mov)

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
./OSXview
```

## Usage

- The window displays real-time system metrics updated every second
- Press Ctrl+C or close the window to exit

## Controls

- Close the window to quit
- The application updates automatically every second

## Implementation Details

- Uses macOS-specific APIs for system metrics:
  - `host_processor_info()` for CPU usage
  - `vm_statistics64` for memory statistics
  - IOKit for network and disk I/O
  - `sysctl()` for load average and process count
- SDL2 for cross-platform graphics rendering
- C++20 standard

## Screenshot

The application resembles the classic OSXview appearance with:
- Dark background
- Colored meters for CPU (red=user, yellow=system, gray=idle)
- Blue memory meter
- Orange swap meter
- Green/purple network graphs
- Blue/orange disk graphs
