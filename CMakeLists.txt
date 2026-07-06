cmake_minimum_required(VERSION 3.16)

project(EdgeVoiceRAG)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(edge_voice_rag
    src/main.cpp
    src/query_router.cpp
    src/logger.cpp
    src/perf_timer.cpp
    src/rag_engine.cpp
    src/app_config.cpp
)

target_include_directories(edge_voice_rag PRIVATE include)