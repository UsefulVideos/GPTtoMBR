cmake_minimum_required(VERSION 3.15)
project(GptToMbr CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(GptToMbr main.cpp)

# Link necessary Windows libraries for low-level console / system checks
target_link_libraries(GptToMbr PRIVATE advapi32)