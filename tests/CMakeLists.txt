# Every test keeps its existing target name and executable location.
function(edge_add_test target)
    cmake_parse_arguments(TEST "" "NAME" "ARGS" ${ARGN})
    if(NOT TEST_NAME)
        set(TEST_NAME "${target}")
    endif()

    add_executable(${target} "${CMAKE_CURRENT_SOURCE_DIR}/${target}.cpp")
    target_link_libraries(${target} PRIVATE edge_voice_rag_core)
    add_test(NAME ${TEST_NAME} COMMAND ${target} ${TEST_ARGS})
    # Most original tests ran from the top-level build directory.
    set_tests_properties(${TEST_NAME} PROPERTIES
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    )
endfunction()

edge_add_test(unit_tests NAME edge_voice_rag_unit_tests)

foreach(test IN ITEMS
    multi_level_response_tests
    edge_response_backend_tests
    llm_protocol_tests
    llm_service_tests
    llm_backend_factory_tests
    llm_backend_stream_tests
    llm_stream_service_tests
    llm_control_server_tests
    rag_stream_protocol_tests
    rag_stream_client_tests
    streaming_answer_backend_tests
    streaming_sentence_buffer_tests
    voice_assistant_tests
    bounded_blocking_queue_tests
    rag_control_protocol_tests
    rag_control_client_tests
    voice_input_pipeline_tests
    continuous_voice_session_tests
    voice_performance_report_tests
    voice_performance_statistics_tests
    wav_audio_reader_tests
    local_rag_llm_backend_tests
    policy_routing_answer_backend_tests
    chinese_text_tokenizer_tests
    bm25_retriever_tests
    dense_vector_index_tests
    hybrid_retriever_tests
    relevance_filtering_retriever_tests
    file_fingerprint_tests
    agent_executor_tests
    llm_agent_planner_tests
    agent_workflow_tests
    agent_answer_backend_tests
    agent_routing_answer_backend_tests
    linux_vehicle_device_tests
    vehicle_tools_tests
)
    edge_add_test(${test})
endforeach()

edge_add_test(retriever_runtime_tests ARGS
    "${PROJECT_SOURCE_DIR}/vector_db/chunks.json"
    "${BGE_MODEL_DIR}"
    "${BGE_INDEX_DIR}"
)
edge_add_test(knowledge_base_loader_tests ARGS
    "${PROJECT_SOURCE_DIR}/vector_db/chunks.json"
)
edge_add_test(board_app_config_tests ARGS
    "${PROJECT_SOURCE_DIR}/config/board_wsl.conf"
)
target_include_directories(multi_level_response_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

set_tests_properties(
    edge_voice_rag_unit_tests
    edge_response_backend_tests
    local_rag_llm_backend_tests
    policy_routing_answer_backend_tests
    bm25_retriever_tests
    PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

if(EDGE_ENABLE_SHERPA_ASR)
    edge_add_test(sherpa_onnx_asr_backend_tests)
    set_tests_properties(sherpa_onnx_asr_backend_tests PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    )
endif()

if(EDGE_ENABLE_SHERPA_TTS)
    edge_add_test(sherpa_onnx_tts_backend_tests ARGS "${SHERPA_ONNX_TTS_MODEL_DIR}")
    set_tests_properties(sherpa_onnx_tts_backend_tests PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    )
endif()

if(EDGE_ENABLE_ALSA)
    edge_add_test(alsa_audio_backend_tests)
    add_test(NAME audio_output_check_smoke COMMAND audio_output_check
        --config "${PROJECT_SOURCE_DIR}/config/board_wsl.conf"
        --device null --mode tone --sample-rate 44100 --repeat 2
    )
    set_tests_properties(audio_output_check_smoke PROPERTIES
        LABELS "board_mainline;audio_output" TIMEOUT 15
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    )
endif()

if(EDGE_ENABLE_SHERPA_VAD)
    edge_add_test(sherpa_onnx_vad_audio_recorder_tests ARGS
        "${PROJECT_SOURCE_DIR}/models/vad/silero_vad.onnx"
    )
    set_tests_properties(sherpa_onnx_vad_audio_recorder_tests PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    )
endif()

if(EDGE_ENABLE_BGE_EMBEDDER)
    # These assets are required for integration tests, not to compile production code.
    foreach(asset IN ITEMS
        "${BGE_MODEL_DIR}/model.onnx"
        "${BGE_MODEL_DIR}/tokenizer.json"
        "${BGE_INDEX_DIR}/index_meta.json"
        "${BGE_INDEX_DIR}/embeddings.f32"
    )
        if(NOT EXISTS "${asset}")
            message(FATAL_ERROR "BGE test asset was not found: ${asset}")
        endif()
    endforeach()

    edge_add_test(bge_embedder_tests ARGS "${BGE_MODEL_DIR}" "${BGE_INDEX_DIR}")
    foreach(test IN ITEMS dense_retriever_tests hybrid_retriever_integration_tests dense_index_consistency_tests)
        edge_add_test(${test} ARGS
            "${PROJECT_SOURCE_DIR}/vector_db/chunks.json"
            "${BGE_MODEL_DIR}"
            "${BGE_INDEX_DIR}"
        )
    endforeach()
endif()


# Host-side board mainline: no sound card, NPU, or Python/ZeroMQ service required.
# Timeout bounds a stuck future/queue wait at the test-process boundary.
set_tests_properties(
    bounded_blocking_queue_tests
    streaming_sentence_buffer_tests
    voice_assistant_tests
    voice_input_pipeline_tests
    continuous_voice_session_tests
    local_rag_llm_backend_tests
    policy_routing_answer_backend_tests
    agent_executor_tests
    llm_agent_planner_tests
    agent_workflow_tests
    agent_answer_backend_tests
    agent_routing_answer_backend_tests
    linux_vehicle_device_tests
    vehicle_tools_tests
    board_app_config_tests
    PROPERTIES LABELS "board_mainline" TIMEOUT 30
)


edge_add_test(voice_lifecycle_tests)
set_tests_properties(voice_lifecycle_tests PROPERTIES
    LABELS "board_mainline;lifecycle" TIMEOUT 30
)
add_test(NAME voice_lifecycle_stress COMMAND voice_lifecycle_tests --stress)
set_tests_properties(voice_lifecycle_stress PROPERTIES
    LABELS "board_mainline;stress" TIMEOUT 120
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
)


edge_add_test(continuous_voice_session_lifecycle_tests)
set_tests_properties(continuous_voice_session_lifecycle_tests PROPERTIES
    LABELS "board_mainline;lifecycle" TIMEOUT 30
)
