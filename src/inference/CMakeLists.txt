target_sources(edge_voice_rag_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/mock_llm_backend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/llm_backend_factory.cpp"
)

if(EDGE_ENABLE_RKLLM)
    target_sources(edge_voice_rag_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/rkllm_backend.cpp")
    target_include_directories(edge_voice_rag_core PRIVATE "${RKLLM_INCLUDE_DIR}")
    target_compile_definitions(edge_voice_rag_core PUBLIC EDGE_ENABLE_RKLLM=1)
    target_link_libraries(edge_voice_rag_core PUBLIC "${RKLLM_RUNTIME_LIBRARY}")
endif()
