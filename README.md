# Xosview Clone for macOS

A C++ clone of the classic Xosview system monitor for macOS, built with SDL2.

## Features

- Real-time CPU usage monitoring (per-core)
- Memory and swap usage display
- Network I/O graphs (in/out)
- Disk I/O graphs (read/write)
- Load average display
- Process count

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
mkdir build
cd build
cmake ..
make
```

3. Run the application:
```bash
./Xosview
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

The application resembles the classic Xosview appearance with:
- Dark background
- Colored meters for CPU (red=user, yellow=system, gray=idle)
- Blue memory meter
- Orange swap meter
- Green/purple network graphs
- Blue/orange disk graphs
