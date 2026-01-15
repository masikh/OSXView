# CMake script to create standalone app bundle

# Create bundle directories
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/OSXView.app/Contents/MacOS")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/OSXView.app/Contents/Resources")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/OSXView.app/Contents/Frameworks")

# Copy executable
file(COPY "${CMAKE_BINARY_DIR}/OSXview.app/Contents/MacOS/OSXview" 
     DESTINATION "${CMAKE_BINARY_DIR}/OSXView.app/Contents/MacOS")

# Copy Info.plist
file(COPY "${CMAKE_SOURCE_DIR}/Info.plist"
     DESTINATION "${CMAKE_BINARY_DIR}/OSXView.app/Contents")

# Create and copy icon
execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/OSXView.app/Contents/Resources")
execute_process(COMMAND python3 ${CMAKE_SOURCE_DIR}/create_icon_simple.py
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Copy resources if they exist
if(EXISTS "${CMAKE_BINARY_DIR}/OSXView.app/Contents/Resources")
    # Resources are already in place, no need to copy
endif()

# Copy frameworks (copy actual files, not symlinks)
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "/opt/homebrew/lib/libSDL2-2.0.0.dylib" "${CMAKE_BINARY_DIR}/OSXView.app/Contents/Frameworks/")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "/opt/homebrew/lib/libSDL2_ttf-2.0.0.dylib" "${CMAKE_BINARY_DIR}/OSXView.app/Contents/Frameworks/")

# Fix library paths
execute_process(COMMAND install_name_tool -change /opt/homebrew/lib/libSDL2-2.0.0.dylib @executable_path/../Frameworks/libSDL2-2.0.0.dylib "${CMAKE_BINARY_DIR}/OSXView.app/Contents/MacOS/OSXview")
execute_process(COMMAND install_name_tool -change /opt/homebrew/lib/libSDL2_ttf-2.0.0.dylib @executable_path/../Frameworks/libSDL2_ttf-2.0.0.dylib "${CMAKE_BINARY_DIR}/OSXView.app/Contents/MacOS/OSXview")

# Fix permissions and sign
execute_process(COMMAND chmod 644 "${CMAKE_BINARY_DIR}/OSXView.app/Contents/Frameworks/libSDL2-2.0.0.dylib")
execute_process(COMMAND chmod 644 "${CMAKE_BINARY_DIR}/OSXView.app/Contents/Frameworks/libSDL2_ttf-2.0.0.dylib")
execute_process(COMMAND xattr -cr "${CMAKE_BINARY_DIR}/OSXView.app")
execute_process(COMMAND codesign --force --deep --sign - "${CMAKE_BINARY_DIR}/OSXView.app")

message(STATUS "App bundle created at: ${CMAKE_BINARY_DIR}/OSXView.app")
