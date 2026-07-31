cmake_minimum_required(VERSION 3.16)

project(EdgeVoiceRAG)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

option(EDGE_ENABLE_ALSA "Enable ALSA audio capture and playback" OFF)
option(EDGE_ENABLE_SHERPA_VAD "Enable sherpa-onnx C++ VAD recorder" OFF)

option(EDGE_ENABLE_RKLLM "Enable Rockchip RKLLM backend" OFF)
set(RKLLM_SDK_ROOT "" CACHE PATH "Path to RKLLM runtime directory")

option(EDGE_ENABLE_SHERPA_ASR "Enable sherpa-onnx C++ ASR backend" OFF)
option(EDGE_ENABLE_SHERPA_TTS "Enable sherpa-onnx C++ TTS backend" OFF)
set(SHERPA_ONNX_ROOT "" CACHE PATH "Path to sherpa-onnx SDK root")
set(
    SHERPA_ONNX_TTS_MODEL_DIR
    "${CMAKE_SOURCE_DIR}/models/tts/vits-melo-tts-zh_en"
    CACHE PATH
    "Path to the sherpa-onnx TTS model used by integration tests"
)

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
    src/cancellable_answer_backend.cpp
    src/rag_control_protocol.cpp
    src/rag_control_client.cpp
    src/voice_input_pipeline.cpp
    src/wav_audio_reader.cpp
    src/wav_audio_recorder.cpp
    src/local_rag_llm_backend.cpp
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
    # 查找rkllm.h所在目录，在设定paths中寻找，找到后幅值给RKLLM_INCLUDE_DIR
    find_path(
        RKLLM_INCLUDE_DIR
        rkllm.h
        PATHS
        "${RKLLM_SDK_ROOT}/include"
        "${RKLLM_SDK_ROOT}/librkllm_api/include"
        NO_DEFAULT_PATH
    )

    # 查找动态库带有rkllmrt相关的动态库文件
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

    # 加入源文件
    target_sources(
        edge_voice_rag_core
        PRIVATE
        src/rkllm_backend.cpp
    )

    # 添加头文件搜索路径 PRIVATE表示只用于编译，不会暴露给其他依赖edge_voice_rag_core的目标
    target_include_directories(
        edge_voice_rag_core
        PRIVATE
        "${RKLLM_INCLUDE_DIR}"
    )

    # 加入条件编译宏
    target_compile_definitions(
        edge_voice_rag_core
        PUBLIC
        EDGE_ENABLE_RKLLM=1
    )

    # 链接动态库
    target_link_libraries(
        edge_voice_rag_core
        PUBLIC
        "${RKLLM_RUNTIME_LIBRARY}"
    )
endif()

if(
    EDGE_ENABLE_SHERPA_ASR
    OR EDGE_ENABLE_SHERPA_TTS
    OR EDGE_ENABLE_SHERPA_VAD
)
    # 使用必须说明sherpa SDK的根目录位置
    if(NOT SHERPA_ONNX_ROOT)
        message(FATAL_ERROR
            "sherpa-onnx ASR/TTS requires SHERPA_ONNX_ROOT"
        )
    endif()

    find_path(
        SHERPA_ONNX_INCLUDE_DIR
        sherpa-onnx/c-api/c-api.h
        PATHS
        "${SHERPA_ONNX_ROOT}/include"
        NO_DEFAULT_PATH
    )

    find_library(
        SHERPA_ONNX_C_LIBRARY
        NAMES sherpa-onnx-c-api
        PATHS
        "${SHERPA_ONNX_ROOT}/lib"
        NO_DEFAULT_PATH
    )

    find_library(
        SHERPA_ONNX_RUNTIME_LIBRARY
        NAMES onnxruntime
        PATHS
        "${SHERPA_ONNX_ROOT}/lib"
        NO_DEFAULT_PATH
    )

    if(NOT SHERPA_ONNX_INCLUDE_DIR)
        message(FATAL_ERROR
            "sherpa-onnx C API headers were not found"
        )
    endif()

    if(NOT SHERPA_ONNX_C_LIBRARY)
        message(FATAL_ERROR
            "libsherpa-onnx-c-api was not found"
        )
    endif()

    if(NOT SHERPA_ONNX_RUNTIME_LIBRARY)
        message(FATAL_ERROR
            "libonnxruntime was not found"
        )
    endif()

    target_include_directories(
        edge_voice_rag_core
        PRIVATE
        "${SHERPA_ONNX_INCLUDE_DIR}"
    )

    target_link_libraries(
        edge_voice_rag_core
        PUBLIC
        "${SHERPA_ONNX_C_LIBRARY}"
        "${SHERPA_ONNX_RUNTIME_LIBRARY}"
    )
endif()

if(EDGE_ENABLE_ALSA)
    find_package(ALSA REQUIRED)

    target_sources(
        edge_voice_rag_core
        PRIVATE
        src/alsa_audio_player.cpp
        src/alsa_audio_recorder.cpp
        src/alsa_pcm_stream_source.cpp
    )

    target_link_directories(
        edge_voice_rag_core
        PRIVATE
        "${ALSA_INCLUDE_DIRS}"
    )

    target_compile_definitions(
        edge_voice_rag_core
        PUBLIC
        EDGE_ENABLE_ALSA=1
    )

    target_link_libraries(
        edge_voice_rag_core
        PUBLIC
        "${ALSA_LIBRARIES}"
    )
endif()

if(EDGE_ENABLE_SHERPA_ASR)
    target_sources(
        edge_voice_rag_core
        PRIVATE
        src/sherpa_onnx_asr_backend.cpp
    )

    target_compile_definitions(
        edge_voice_rag_core
        PUBLIC
        EDGE_ENABLE_SHERPA_ASR=1
    )
endif()

if(EDGE_ENABLE_SHERPA_TTS)
    target_sources(
        edge_voice_rag_core
        PRIVATE
        src/sherpa_onnx_tts_backend.cpp
    )

    target_compile_definitions(
        edge_voice_rag_core
        PUBLIC
        EDGE_ENABLE_SHERPA_TTS=1
    )
endif()

if(EDGE_ENABLE_SHERPA_VAD)
    target_sources(
        edge_voice_rag_core
        PRIVATE
        src/sherpa_onnx_vad_audio_recorder.cpp
    )

    target_compile_definitions(
        edge_voice_rag_core
        PUBLIC
        EDGE_ENABLE_SHERPA_VAD=1
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

if(EDGE_ENABLE_SHERPA_ASR)
    add_executable(
        sherpa_asr_demo
        src/sherpa_asr_demo.cpp
    )

    target_link_libraries(
        sherpa_asr_demo
        PRIVATE
        edge_voice_rag_core
    )
endif()

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

add_executable(
    rag_control_protocol_tests
    tests/rag_control_protocol_tests.cpp
)
target_link_libraries(
    rag_control_protocol_tests
    PRIVATE
    edge_voice_rag_core
)

add_executable(
    rag_control_client_tests
    tests/rag_control_client_tests.cpp
)
target_link_libraries(
    rag_control_client_tests
    PRIVATE
    edge_voice_rag_core
)

add_executable(
    voice_input_pipeline_tests
    tests/voice_input_pipeline_tests.cpp
)
target_link_libraries(
    voice_input_pipeline_tests
    PRIVATE
    edge_voice_rag_core
    Threads::Threads
)

add_executable(
    wav_audio_reader_tests
    tests/wav_audio_reader_tests.cpp
)
target_link_libraries(
    wav_audio_reader_tests
    PRIVATE
    edge_voice_rag_core
)

add_executable(
    local_rag_llm_backend_tests
    tests/local_rag_llm_backend_tests.cpp
)
target_link_libraries(
    local_rag_llm_backend_tests
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
add_test(
    NAME rag_control_protocol_tests
    COMMAND rag_control_protocol_tests
)
add_test(
    NAME rag_control_client_tests
    COMMAND rag_control_client_tests
)
add_test(
    NAME voice_input_pipeline_tests
    COMMAND voice_input_pipeline_tests
)
add_test(
    NAME wav_audio_reader_tests
    COMMAND wav_audio_reader_tests
)
add_test(
    NAME local_rag_llm_backend_tests
    COMMAND local_rag_llm_backend_tests
)

if(EDGE_ENABLE_SHERPA_ASR)
    add_executable(
        sherpa_onnx_asr_backend_tests
        tests/sherpa_onnx_asr_backend_tests.cpp
    )

    target_link_libraries(
        sherpa_onnx_asr_backend_tests
        PRIVATE
        edge_voice_rag_core
    )

    add_test(
        NAME sherpa_onnx_asr_backend_tests
        COMMAND sherpa_onnx_asr_backend_tests
    )
endif()

if(EDGE_ENABLE_SHERPA_TTS)
    add_executable(
        sherpa_onnx_tts_backend_tests
        tests/sherpa_onnx_tts_backend_tests.cpp
    )

    target_link_libraries(
        sherpa_onnx_tts_backend_tests
        PRIVATE
        edge_voice_rag_core
    )

    add_test(
        NAME sherpa_onnx_tts_backend_tests
        COMMAND
            sherpa_onnx_tts_backend_tests
            "${SHERPA_ONNX_TTS_MODEL_DIR}"
    )
endif()

if(EDGE_ENABLE_ALSA)
    add_executable(
        alsa_audio_backend_tests
        tests/alsa_audio_backend_tests.cpp
    )

    target_link_libraries(
        alsa_audio_backend_tests
        PRIVATE
        edge_voice_rag_core
    )

    add_test(
        NAME alsa_audio_backend_tests
        COMMAND alsa_audio_backend_tests
    )
endif()

if(EDGE_ENABLE_SHERPA_VAD)
    add_executable(
        sherpa_onnx_vad_audio_recorder_tests
        tests/sherpa_onnx_vad_audio_recorder_tests.cpp
    )

    target_link_libraries(
        sherpa_onnx_vad_audio_recorder_tests
        PRIVATE
        edge_voice_rag_core
    )

    add_test(
        NAME sherpa_onnx_vad_audio_recorder_tests
        COMMAND
            sherpa_onnx_vad_audio_recorder_tests
            "${CMAKE_SOURCE_DIR}/models/vad/silero_vad.onnx"
    )

    set_tests_properties(
        sherpa_onnx_vad_audio_recorder_tests
        PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()

set_tests_properties(edge_voice_rag_unit_tests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(
    edge_response_backend_tests
    local_rag_llm_backend_tests
    sherpa_onnx_asr_backend_tests
    sherpa_onnx_tts_backend_tests
    PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
