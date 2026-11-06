cmake_minimum_required(VERSION 3.16)

project(EdgeVoiceRAG LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Keep ./build/<program> paths used by the existing scripts.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

include(cmake/EdgeOptions.cmake)
include(cmake/EdgeDependencies.cmake)
include(CTest)

add_subdirectory(src)
add_subdirectory(tools)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
