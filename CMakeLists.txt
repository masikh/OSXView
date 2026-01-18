cmake_minimum_required(VERSION 3.16)
project(OSXview)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13")

option(OSXVIEW_PROFILE "Enable lightweight profiling logs" OFF)

set(APP_BUNDLE_DIR "${CMAKE_BINARY_DIR}/OSXView.app")
set(APP_CONTENTS_DIR "${APP_BUNDLE_DIR}/Contents")
set(APP_MACOS_DIR "${APP_CONTENTS_DIR}/MacOS")
set(APP_RESOURCES_DIR "${APP_CONTENTS_DIR}/Resources")

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
add_executable(OSXview MACOSX_BUNDLE
    main.cpp
    SystemMetrics.cpp
    Display.cpp
)

if(OSXVIEW_PROFILE)
    target_compile_definitions(OSXview PRIVATE OSXVIEW_PROFILE)
    message(STATUS "OSXVIEW_PROFILE enabled")
endif()

# Set bundle properties
set_target_properties(OSXview PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist"
    MACOSX_BUNDLE_BUNDLE_NAME "OSXView"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_IDENTIFIER "com.local.osxview"
    MACOSX_BUNDLE_ICON_FILE "AppIcon"
)

# Copy icon to bundle resources
# Note: Resources are copied in create_app_bundle target instead

# Add a script to create the standalone app bundle
add_custom_target(bundle_script ALL
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/create_bundle.cmake
    DEPENDS OSXview
    COMMENT "Creating standalone app bundle"
)

# Link libraries
target_link_libraries(OSXview
    ${SDL2_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
    ${IOKIT_LIBRARY}
    ${COREFOUNDATION_LIBRARY}
    ${SYSTEMCONFIGURATION_LIBRARY}
)

# Add library paths
target_link_directories(OSXview PRIVATE
    ${SDL2_LIBRARY_DIRS}
    ${SDL2_TTF_LIBRARY_DIRS}
)

# Set compiler flags
target_compile_options(OSXview PRIVATE -Wall -Wextra)
