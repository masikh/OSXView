#!/bin/bash

# Build script for OSXview on macOS

echo "Building OSXview..."

# Check if SDL2 is installed
if ! brew list sdl2 &> /dev/null; then
    echo "SDL2 not found. Installing..."
    brew install sdl2
fi

# Check if SDL2_ttf is installed
if ! brew list sdl2_ttf &> /dev/null; then
    echo "SDL2_ttf not found. Installing..."
    brew install sdl2_ttf
fi

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
echo "Building..."
make -j$(sysctl -n hw.ncpu)

echo "Build complete! Run with: ./build/OSXview"
