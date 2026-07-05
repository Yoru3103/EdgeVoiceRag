cmake_minimum_required(VERSION 3.16)

project(EdgeVoiceRAG)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(edge_voice_rag
    src/main.cpp
)