cmake_minimum_required(VERSION 3.16)
project(Xosview)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SDL2
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
include_directories(${SDL2_INCLUDE_DIRS})

# Find SDL2_ttf
pkg_check_modules(SDL2_TTF REQUIRED sdl2_ttf)
include_directories(${SDL2_TTF_INCLUDE_DIRS})

# Find IOKit and CoreFoundation
find_library(IOKIT_LIBRARY IOKit)
find_library(COREFOUNDATION_LIBRARY CoreFoundation)
find_library(SYSTEMCONFIGURATION_LIBRARY SystemConfiguration)

# Add executable
add_executable(Xosview 
    main.cpp
    SystemMetrics.cpp
    Display.cpp
)

# Link libraries
target_link_libraries(Xosview 
    ${SDL2_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
    ${IOKIT_LIBRARY}
    ${COREFOUNDATION_LIBRARY}
    ${SYSTEMCONFIGURATION_LIBRARY}
)

# Add library paths
target_link_directories(Xosview PRIVATE 
    ${SDL2_LIBRARY_DIRS}
    ${SDL2_TTF_LIBRARY_DIRS}
)

# Set compiler flags
target_compile_options(Xosview PRIVATE -Wall -Wextra)
