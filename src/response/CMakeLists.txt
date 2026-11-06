target_sources(edge_voice_rag_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/query_router.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/query_classifier.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/multi_level_response_system.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/edge_response_backend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/streaming_answer_backend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/cancellable_answer_backend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/local_rag_llm_backend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/response_policy.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/policy_routing_answer_backend.cpp"
)
