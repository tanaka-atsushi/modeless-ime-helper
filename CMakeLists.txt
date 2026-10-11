cmake_minimum_required(VERSION 3.20)
project(ModelessImeHelper LANGUAGES CXX)

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

add_executable(ModelessImeHelper WIN32
  src/main.cpp
  src/KeyboardHook.cpp
  src/ImeState.cpp
  src/ImeSwitcher.cpp
  src/PasswordFocus.cpp
  src/app.rc
  # CMake embeds *.manifest sources via mt.exe (PerMonitorV2 DPI, asInvoker).
  src/app.manifest
)

target_compile_definitions(ModelessImeHelper PRIVATE UNICODE _UNICODE)
# res/ holds the .ico files that src/app.rc names.
target_include_directories(ModelessImeHelper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/res)

if(MSVC)
  target_compile_options(ModelessImeHelper PRIVATE /W4 /utf-8 /EHsc)
endif()

target_link_libraries(ModelessImeHelper PRIVATE
  user32 shell32 imm32 advapi32 ole32 oleaut32 uiautomationcore
)
