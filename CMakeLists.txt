cmake_minimum_required(VERSION 3.16)

project(EdgeVoiceRAG)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(edge_voice_rag_core
    src/query_router.cpp
    src/logger.cpp
    src/perf_timer.cpp
    src/rag_engine.cpp
    src/app_config.cpp
    src/command_line_options.cpp
    src/rag_client_zmq.cpp
)

target_include_directories(edge_voice_rag_core PUBLIC include)  # PUBLIC: edge_voice_rag_core 自己需要 include/ 链接 edge_voice_rag_core 的目标也需要 include/

target_link_libraries(edge_voice_rag_core PUBLIC zmq)

add_executable(edge_voice_rag
    src/main.cpp
)

target_link_libraries(edge_voice_rag PRIVATE edge_voice_rag_core)   # PUBLIC：edge_voice_rag 自己链接 edge_voice_rag_core 这个链接不继续向外传播

add_executable(unit_tests
    tests/unit_tests.cpp
)

target_link_libraries(unit_tests PRIVATE edge_voice_rag_core)

add_executable(zmq_rep_server
    src/zmq_rep_server.cpp
)

target_link_libraries(zmq_rep_server PRIVATE zmq)

add_executable(zmq_req_client
    src/zmq_req_client.cpp
)

target_link_libraries(zmq_req_client PRIVATE zmq)

add_executable(rag_server
    src/rag_server.cpp
)

target_link_libraries(rag_server PRIVATE edge_voice_rag_core zmq)

add_executable(rag_client
    src/rag_client.cpp
)

target_link_libraries(rag_client PRIVATE zmq)