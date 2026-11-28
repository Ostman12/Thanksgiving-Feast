cmake_minimum_required(VERSION 3.20)
project(FeastMode CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------ dependencies
# Fetched once at configure time, pinned to release tags (docs/design.md).
# SOURCE_SUBDIR points at a folder that doesn't exist, so CMake only downloads
# them; the header-only targets are declared below. (Crow's own CMake would go
# looking for an installed Asio.)
include(FetchContent)

FetchContent_Declare(crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG        v1.3.5
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  do-not-build)
FetchContent_Declare(asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG        asio-1-38-2
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  do-not-build)
FetchContent_Declare(nlohmann_json
  URL      https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR do-not-build)
FetchContent_MakeAvailable(crow asio nlohmann_json)

add_library(dep_json INTERFACE)
target_include_directories(dep_json SYSTEM INTERFACE ${nlohmann_json_SOURCE_DIR}/single_include)

add_library(dep_crow INTERFACE)
# Asio's headers are in include/ at the repo root (asio/include is a symlink,
# which Windows checks out as a plain text file).
target_include_directories(dep_crow SYSTEM INTERFACE
  ${crow_SOURCE_DIR}/include
  ${asio_SOURCE_DIR}/include)
target_compile_definitions(dep_crow INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
if(WIN32)
  target_compile_definitions(dep_crow INTERFACE _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(dep_crow INTERFACE ws2_32 mswsock)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(dep_crow INTERFACE Threads::Threads)
endif()

# ------------------------------------------------------------------ our code
if(MSVC)
  # /external:W0 silences warnings inside the fetched (SYSTEM) headers only.
  add_compile_options(/W4 /permissive- /utf-8 /bigobj /external:W0)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Game logic: no networking, so it can be unit tested on its own.
add_library(feast_game
  src/game/text.cpp
  src/game/room_code.cpp
  src/game/room.cpp
  src/game/room_mystery.cpp
  src/game/mystery.cpp
  src/game/content.cpp
  src/game/room_wyr.cpp
  src/game/wyr.cpp
  src/game/att.cpp
  src/game/room_att.cpp
  src/game/awards.cpp
  src/game/room_awards.cpp
  src/game/avatar.cpp
)
target_include_directories(feast_game PUBLIC src)

# Protocol: JSON in/out and connection bookkeeping. No sockets either.
add_library(feast_protocol
  src/server/protocol.cpp
  src/server/room_manager.cpp
)
target_link_libraries(feast_protocol PUBLIC feast_game dep_json)

# The server: the only code that touches Crow and real sockets.
add_executable(feast_server src/server/main.cpp)
target_link_libraries(feast_server PRIVATE feast_protocol dep_crow)

enable_testing()
add_executable(feast_tests
  tests/test_main.cpp
  tests/test_text.cpp
  tests/test_room_code.cpp
  tests/test_room.cpp
  tests/test_phases.cpp
  tests/test_mystery.cpp
  tests/test_wyr.cpp
  tests/test_server.cpp
  tests/test_att.cpp
  tests/test_awards.cpp
  tests/test_avatar.cpp
)
target_link_libraries(feast_tests PRIVATE feast_protocol)
add_test(NAME feast_tests COMMAND feast_tests)
