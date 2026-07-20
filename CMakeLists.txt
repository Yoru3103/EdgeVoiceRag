cmake_minimum_required(VERSION 3.16)

project(EdgeVoiceRAG)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json REQUIRED)

add_library(edge_voice_rag_core
    src/query_router.cpp
    src/query_classifier.cpp
    src/multi_level_response_system.cpp
    src/logger.cpp
    src/perf_timer.cpp
    src/rag_engine.cpp
    src/app_config.cpp
    src/command_line_options.cpp
    src/rag_client_zmq.cpp
    src/rag_response_parser.cpp
    src/zmq_text_client.cpp
    src/edge_response_backend.cpp
    src/llm_protocol.cpp
    src/mock_llm_backend.cpp
    src/llm_service.cpp
)

target_include_directories(edge_voice_rag_core PUBLIC include)  # PUBLIC: edge_voice_rag_core 自己需要 include/ 链接 edge_voice_rag_core 的目标也需要 include/

target_link_libraries(edge_voice_rag_core PUBLIC zmq nlohmann_json::nlohmann_json)

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

add_executable(
    llm_server
    src/llm_server.cpp
)

target_link_libraries(
    llm_server
    PRIVATE
    edge_voice_rag_core
)

add_executable(
    multi_level_response_tests
    tests/multi_level_response_tests.cpp
)

target_include_directories(
    multi_level_response_tests
    PRIVATE
    tests
)

target_link_libraries(
    multi_level_response_tests
    PRIVATE
    edge_voice_rag_core
)

add_executable(
    edge_response_backend_tests
    tests/edge_response_backend_tests.cpp
)

target_link_libraries(
    edge_response_backend_tests
    PRIVATE
    edge_voice_rag_core
)

add_executable(
    llm_protocol_tests
    tests/llm_protocol_tests.cpp
)

target_link_libraries(
    llm_protocol_tests
    PRIVATE
    edge_voice_rag_core
)

add_executable(
    llm_service_tests
    tests/llm_service_tests.cpp
)

target_link_libraries(
    llm_service_tests
    PRIVATE
    edge_voice_rag_core
)

enable_testing()
add_test(
    NAME edge_voice_rag_unit_tests
    COMMAND unit_tests
)
add_test(
    NAME multi_level_response_tests
    COMMAND multi_level_response_tests
)
add_test(
    NAME edge_response_backend_tests
    COMMAND edge_response_backend_tests
)
add_test(
    NAME llm_protocol_tests
    COMMAND llm_protocol_tests
)
add_test(
    NAME llm_service_tests
    COMMAND llm_service_tests
)

set_tests_properties(edge_voice_rag_unit_tests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(
    edge_response_backend_tests
    PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
