target_sources(edge_voice_rag_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/wav_audio_reader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/wav_audio_recorder.cpp"
)

if(EDGE_ENABLE_ALSA)
    target_sources(edge_voice_rag_core PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/alsa_audio_player.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/alsa_audio_recorder.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/alsa_pcm_stream_source.cpp"
    )
    target_include_directories(edge_voice_rag_core PRIVATE ${ALSA_INCLUDE_DIRS})
    target_compile_definitions(edge_voice_rag_core PUBLIC EDGE_ENABLE_ALSA=1)
    target_link_libraries(edge_voice_rag_core PUBLIC ${ALSA_LIBRARIES})
endif()
