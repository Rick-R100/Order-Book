cmake_minimum_required(VERSION 3.14)
project(order_book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(orderbook src/order_book.cpp)
target_include_directories(orderbook PUBLIC include)
if(NOT MSVC)
  target_compile_options(orderbook PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(bench bench/bench.cpp)  add_executable(bench bench/bench.cpp)
target_link_libraries(bench PRIVATE orderbook) target_link_libraries(bench PRIVATE orderbook)

enable_testing()
add_executable(test_order_book tests/test_order_book.cpp)
target_link_libraries(test_order_book PRIVATE orderbook)
add_test(NAME order_book COMMAND test_order_book)
