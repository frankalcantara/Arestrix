cmake_minimum_required(VERSION 3.20)
project(New Taskflow Graph CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/std:c++20 /EHsc /W4 /O2 /permissive-)
else()
  add_compile_options(-std=c++20 -Wall -Wextra -O3 -pthread)
endif()

include_directories(include)
add_executable(pipeline_app main.cpp)
find_package(Threads REQUIRED)
target_link_libraries(pipeline_app PRIVATE Threads::Threads)
