# Optional speech adapters are compiled only when their SDK feature is enabled.

if(EDGE_ENABLE_SHERPA_ASR)
    target_sources(edge_voice_rag_core PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/sherpa_onnx_asr_backend.cpp"
    )
    target_compile_definitions(edge_voice_rag_core PUBLIC EDGE_ENABLE_SHERPA_ASR=1)
endif()

if(EDGE_ENABLE_SHERPA_TTS)
    target_sources(edge_voice_rag_core PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/sherpa_onnx_tts_backend.cpp"
    )
    target_compile_definitions(edge_voice_rag_core PUBLIC EDGE_ENABLE_SHERPA_TTS=1)
endif()

if(EDGE_ENABLE_SHERPA_VAD)
    target_sources(edge_voice_rag_core PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/sherpa_onnx_vad_audio_recorder.cpp"
    )
    target_compile_definitions(edge_voice_rag_core PUBLIC EDGE_ENABLE_SHERPA_VAD=1)
endif()

if(EDGE_ENABLE_SHERPA_ASR OR EDGE_ENABLE_SHERPA_TTS OR EDGE_ENABLE_SHERPA_VAD)
    target_include_directories(edge_voice_rag_core PRIVATE "${SHERPA_ONNX_INCLUDE_DIR}")
    target_link_libraries(edge_voice_rag_core PUBLIC
        "${SHERPA_ONNX_C_LIBRARY}"
        "${SHERPA_ONNX_RUNTIME_LIBRARY}"
    )
endif()
