cmake_minimum_required(VERSION 3.16)
project(Xosview)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
add_executable(Xosview MACOSX_BUNDLE
    main.cpp
    SystemMetrics.cpp
    Display.cpp
)

# Set bundle properties
set_target_properties(Xosview PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist"
    MACOSX_BUNDLE_BUNDLE_NAME "OSXView"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_IDENTIFIER "com.local.osxview"
    MACOSX_BUNDLE_ICON_FILE "AppIcon"
)

# Copy icon to bundle resources
add_custom_command(TARGET Xosview POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/OSXView.app/Contents/Resources"
    "${APP_RESOURCES_DIR}"
)

# Create a standalone app bundle after build
add_custom_target(create_app_bundle ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:Xosview> "${APP_MACOS_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/Info.plist" "${APP_CONTENTS_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_RESOURCES_DIR}"
    COMMAND cp -r "${CMAKE_SOURCE_DIR}/OSXView.app/Contents/Resources"/* "${APP_RESOURCES_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_CONTENTS_DIR}/Frameworks"
    COMMAND cp /opt/homebrew/lib/libSDL2-2.0.0.dylib "${APP_CONTENTS_DIR}/Frameworks/"
    COMMAND cp /opt/homebrew/lib/libSDL2_ttf-2.0.0.dylib "${APP_CONTENTS_DIR}/Frameworks/"
    COMMAND install_name_tool -change /opt/homebrew/lib/libSDL2-2.0.0.dylib "@executable_path/../Frameworks/libSDL2-2.0.0.dylib" "${APP_MACOS_DIR}/Xosview"
    COMMAND install_name_tool -change /opt/homebrew/lib/libSDL2_ttf-2.0.0.dylib "@executable_path/../Frameworks/libSDL2_ttf-2.0.0.dylib" "${APP_MACOS_DIR}/Xosview"
    COMMAND codesign --force --deep --sign - "${APP_BUNDLE_DIR}"
    DEPENDS Xosview
    COMMENT "Creating standalone app bundle with bundled frameworks"
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
