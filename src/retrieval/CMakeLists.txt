target_sources(edge_voice_rag_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/bm25_retriever.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/chinese_text_tokenizer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dense_vector_index.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/knowledge_base_loader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/hybrid_retriever.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/retriever_runtime.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/relevance_filtering_retriever.cpp"
)

if(EDGE_ENABLE_BGE_EMBEDDER)
    target_sources(edge_voice_rag_core PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/bge_embedder.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/dense_retriever.cpp"
    )
    target_include_directories(edge_voice_rag_core PRIVATE "${ONNXRUNTIME_INCLUDE_DIR}")
    target_compile_definitions(edge_voice_rag_core PUBLIC EDGE_ENABLE_BGE_EMBEDDER=1)
    target_link_libraries(edge_voice_rag_core PUBLIC tokenizers_cpp "${ONNXRUNTIME_LIBRARY}")
endif()
