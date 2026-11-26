cmake_minimum_required(VERSION 3.20)
project(FeastMode CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
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
)
target_include_directories(feast_game PUBLIC src)

enable_testing()
add_executable(feast_tests
  tests/test_main.cpp
  tests/test_text.cpp
  tests/test_room_code.cpp
  tests/test_room.cpp
  tests/test_phases.cpp
  tests/test_mystery.cpp
)
target_link_libraries(feast_tests PRIVATE feast_game)
add_test(NAME feast_tests COMMAND feast_tests)
