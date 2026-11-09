cmake_minimum_required(VERSION 3.15)
project(GptToMbr CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(GptToMbr main.cpp)

if(WIN32)
    target_link_libraries(GptToMbr PRIVATE advapi32)
endif()

set(GPTTOMBR_RELEASE_VERSION "" CACHE STRING "Version tag to publish with the release target")

if(UNIX)
    find_program(BASH_EXECUTABLE bash)
    if(BASH_EXECUTABLE)
        add_custom_target(release
            COMMAND "${BASH_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/release.sh" "${GPTTOMBR_RELEASE_VERSION}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            USES_TERMINAL
            COMMENT "Creating GitHub release ${GPTTOMBR_RELEASE_VERSION}"
        )
    else()
        message(STATUS "Bash not found; the GitHub release target is unavailable.")
    endif()
endif()