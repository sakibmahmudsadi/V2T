cmake_minimum_required(VERSION 3.20)
project(ParakeetType CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Standalone executables: no DLL next to the exe, nothing to install but the
# binary itself. The controller must stay cheap to start and easy to ship.
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")

# ---------------------------------------------------------------------------
# ptcore — portable logic + the engine binding.
# No OS headers are allowed in src/core; src/engine uses the Win32 loader.
# ---------------------------------------------------------------------------
add_library(ptcore STATIC
  src/core/log.cpp
  src/core/text.cpp
  src/core/wav.cpp
  src/engine/transcribe_api.cpp
)
target_include_directories(ptcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(WIN32)
  target_link_libraries(ptcore PUBLIC advapi32)
endif()

# ---------------------------------------------------------------------------
# ParakeetTypeWorker — the model host.
# ---------------------------------------------------------------------------
add_executable(ParakeetTypeWorker
  src/worker/main.cpp
  src/platform/win32/paths.cpp
)
target_include_directories(ParakeetTypeWorker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ParakeetTypeWorker PRIVATE ptcore)
if(WIN32)
  target_link_libraries(ParakeetTypeWorker PRIVATE user32)
endif()
