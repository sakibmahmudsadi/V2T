cmake_minimum_required(VERSION 3.20)
project(V2T CXX)

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
  src/core/config.cpp
  src/core/ipc.cpp
  src/core/json.cpp
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
# ptwin32 — the Windows backends (hotkey, capture, typing, mic button).
# ---------------------------------------------------------------------------
add_library(ptwin32 STATIC
  src/platform/win32/audio.cpp
  src/platform/win32/hotkey.cpp
  src/platform/win32/mic_button.cpp
  src/platform/win32/mic_glyph.cpp
  src/platform/win32/paths.cpp
  src/platform/win32/tray.cpp
  src/platform/win32/typing.cpp
  src/platform/win32/worker_client.cpp
)
target_include_directories(ptwin32 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(WIN32)
  target_link_libraries(ptwin32 PUBLIC user32 gdi32 shell32 winmm dwmapi)
endif()

# ---------------------------------------------------------------------------
# V2TWorker — the model host.
# ---------------------------------------------------------------------------
add_executable(V2TWorker
  src/worker/main.cpp
)
target_include_directories(V2TWorker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# ptwin32 depends on ptcore, so it comes first for the static linker.
target_link_libraries(V2TWorker PRIVATE ptwin32 ptcore)

# ---------------------------------------------------------------------------
# V2T — the always-on controller.
# ---------------------------------------------------------------------------
add_executable(V2T
  src/controller/main.cpp
)
target_include_directories(V2T PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(V2T PRIVATE ptwin32 ptcore)
