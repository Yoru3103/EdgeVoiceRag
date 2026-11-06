target_sources(edge_voice_rag_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/streaming_sentence_buffer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/voice_assistant.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/voice_input_pipeline.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/continuous_voice_session.cpp"
)
