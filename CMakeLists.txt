cmake_minimum_required(VERSION 3.16)

project(EdgeVoiceRAG)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

option(EDGE_ENABLE_RKLLM "Enable Rockchip RKLLM backend" OFF)
set(RKLLM_SDK_ROOT "" CACHE PATH "Path to RKLLM runtime directory")

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
    src/rag_stream_client.cpp
    src/llm_protocol.cpp
    src/mock_llm_backend.cpp
    src/llm_service.cpp
    src/llm_backend_factory.cpp
    src/llm_stream_service.cpp
    src/llm_control_server.cpp
    src/rag_stream_protocol.cpp
    src/streaming_answer_backend.cpp
    src/streaming_sentence_buffer.cpp
    src/voice_assistant.cpp
)

target_include_directories(edge_voice_rag_core PUBLIC include)  # PUBLIC: edge_voice_rag_core 自己需要 include/ 链接 edge_voice_rag_core 的目标也需要 include/

target_link_libraries(
    edge_voice_rag_core
    PUBLIC
    zmq
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(EDGE_ENABLE_RKLLM)
    find_path(
        RKLLM_INCLUDE_DIR
        rkllm.h
        PATHS
        "${RKLLM_SDK_ROOT}/include"
        "${RKLLM_SDK_ROOT}/librkllm_api/include"
        NO_DEFAULT_PATH
    )

    find_library(
        RKLLM_RUNTIME_LIBRARY
        NAMES rkllmrt
        PATHS
        "${RKLLM_SDK_ROOT}/lib"
        "${RKLLM_SDK_ROOT}/lib64"
        "${RKLLM_SDK_ROOT}/${CMAKE_SYSTEM_PROCESSOR}"
        NO_DEFAULT_PATH
    )

    if(NOT RKLLM_INCLUDE_DIR)
        message(FATAL_ERROR
            "rkllm.h was not found under RKLLM_SDK_ROOT"
        )
    endif()

    if(NOT RKLLM_RUNTIME_LIBRARY)
        message(FATAL_ERROR
            "librkllmrt.so was not found under RKLLM_SDK_ROOT"
        )
    endif()

    target_sources(
        edge_voice_rag_core
        PRIVATE
        src/rkllm_backend.cpp
    )

    target_include_directories(
        edge_voice_rag_core
        PRIVATE
        "${RKLLM_INCLUDE_DIR}"
    )

    target_compile_definitions(
        edge_voice_rag_core
        PUBLIC
        EDGE_ENABLE_RKLLM=1
    )

    target_link_libraries(
        edge_voice_rag_core
        PUBLIC
        "${RKLLM_RUNTIME_LIBRARY}"
    )
endif()

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

add_executable(
    llm_backend_factory_tests
    tests/llm_backend_factory_tests.cpp
)

target_link_libraries(
    llm_backend_factory_tests
    PRIVATE
    edge_voice_rag_core
)

add_executable(
    llm_backend_stream_tests
    tests/llm_backend_stream_tests.cpp
)

target_link_libraries(
    llm_backend_stream_tests
    PRIVATE
    edge_voice_rag_core
)

add_executable(
    llm_stream_service_tests
    tests/llm_stream_service_tests.cpp
)

target_link_libraries(
    llm_stream_service_tests
    PRIVATE
    edge_voice_rag_core
    Threads::Threads
)

add_executable(
    llm_control_server_tests
    tests/llm_control_server_tests.cpp
)

target_link_libraries(
    llm_control_server_tests
    PRIVATE
    edge_voice_rag_core
    Threads::Threads
)

add_executable(
    rag_stream_protocol_tests
    tests/rag_stream_protocol_tests.cpp
)
target_link_libraries(
    rag_stream_protocol_tests
    PRIVATE
    edge_voice_rag_core
)

add_executable(
    rag_stream_client_tests
    tests/rag_stream_client_tests.cpp
)
target_link_libraries(
    rag_stream_client_tests
    PRIVATE
    edge_voice_rag_core
    Threads::Threads
)

add_executable(
    streaming_answer_backend_tests
    tests/streaming_answer_backend_tests.cpp
)
target_link_libraries(
    streaming_answer_backend_tests
    PRIVATE
    edge_voice_rag_core
)

add_executable(
    streaming_sentence_buffer_tests
    tests/streaming_sentence_buffer_tests.cpp
)
target_link_libraries(
    streaming_sentence_buffer_tests
    PRIVATE
    edge_voice_rag_core
)

add_executable(
    voice_assistant_tests
    tests/voice_assistant_tests.cpp
)
target_link_libraries(
    voice_assistant_tests
    PRIVATE
    edge_voice_rag_core
)

add_executable(
    bounded_blocking_queue_tests
    tests/bounded_blocking_queue_tests.cpp
)
target_link_libraries(
    bounded_blocking_queue_tests
    PRIVATE
    edge_voice_rag_core
    Threads::Threads
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
add_test(
    NAME llm_backend_factory_tests
    COMMAND llm_backend_factory_tests
)
add_test(
    NAME llm_backend_stream_tests
    COMMAND llm_backend_stream_tests
)
add_test(
    NAME llm_stream_service_tests
    COMMAND llm_stream_service_tests
)
add_test(
    NAME llm_control_server_tests
    COMMAND llm_control_server_tests
)
add_test(
    NAME rag_stream_protocol_tests
    COMMAND rag_stream_protocol_tests
)
add_test(
    NAME rag_stream_client_tests
    COMMAND rag_stream_client_tests
)
add_test(
    NAME streaming_answer_backend_tests
    COMMAND streaming_answer_backend_tests
)
add_test(
    NAME streaming_sentence_buffer_tests
    COMMAND streaming_sentence_buffer_tests
)
add_test(
    NAME voice_assistant_tests
    COMMAND voice_assistant_tests
)
add_test(
    NAME bounded_blocking_queue_tests
    COMMAND bounded_blocking_queue_tests
)

set_tests_properties(edge_voice_rag_unit_tests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(
    edge_response_backend_tests
    PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
