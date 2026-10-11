cmake_minimum_required(VERSION 3.20)
project(ime2nd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-config generators (Ninja) default to an empty build type; this app
# is meant to be built optimized.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# /MT (static CRT) so the exe has no VC++ redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(ime2nd WIN32
  src/main.cpp
  src/KeyboardHook.cpp
  src/ImeState.cpp
  src/ImeSwitcher.cpp
  # CMake embeds *.manifest sources via mt.exe (PerMonitorV2 DPI, asInvoker).
  src/app.manifest
)

target_compile_definitions(ime2nd PRIVATE UNICODE _UNICODE)

if(MSVC)
  target_compile_options(ime2nd PRIVATE /W4 /utf-8 /EHsc)
endif()

target_link_libraries(ime2nd PRIVATE
  user32 shell32 imm32 advapi32
)
