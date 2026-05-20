# The Flutter tooling requires that developers have CMake 3.10 or later
# installed. You should not increase this version, as doing so will cause
# the plugin to fail to compile for some customers of the plugin.
cmake_minimum_required(VERSION 3.10)

project(bmc_video_windows_library VERSION 0.0.1 LANGUAGES C)

add_library(bmc_video_windows SHARED
  "bmc_video_windows.c"
)

set_target_properties(bmc_video_windows PROPERTIES
  PUBLIC_HEADER bmc_video_windows.h
  OUTPUT_NAME "bmc_video_windows"
)

target_compile_definitions(bmc_video_windows PUBLIC DART_SHARED_LIB)

if (WIN32)
  # Force the .c file to genuinely compile as C on MSVC
  set_source_files_properties("bmc_video_windows.c" PROPERTIES LANGUAGE C)
  set_target_properties(bmc_video_windows PROPERTIES LINKER_LANGUAGE C)

  # Media Foundation + COM + WIC (for JPEG encoding) + shlwapi (for SHCreateMemStream)
  target_link_libraries(bmc_video_windows PRIVATE
    ole32
    uuid
    mfplat
    mf
    mfreadwrite
    mfuuid
    windowscodecs
    shlwapi
  )
  target_compile_options(bmc_video_windows PRIVATE /WX- /TC)
endif()

# Copy the built DLL into the Flutter app's output directory.
install(TARGETS bmc_video_windows
  RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}"
)
