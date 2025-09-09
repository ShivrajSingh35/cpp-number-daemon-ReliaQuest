cmake_minimum_required(VERSION 3.10)
project(cpp-number-daemon-ReliaQuest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Main programs
add_executable(numd daemon.cpp store.cpp)
add_executable(numctl cli.cpp store.cpp)

# Tests
enable_testing()
find_package(GTest REQUIRED)

add_executable(test_store tests/test_store.cpp store.cpp)
target_link_libraries(test_store GTest::GTest GTest::Main pthread)
add_test(NAME store_tests COMMAND test_store)
