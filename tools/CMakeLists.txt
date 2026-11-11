foreach(tool IN ITEMS retrieval_benchmark voice_performance_summary)
    add_executable(${tool} ${tool}.cpp)
    target_link_libraries(${tool} PRIVATE edge_voice_rag_core)
endforeach()


if(EDGE_ENABLE_BGE_EMBEDDER)
    add_executable(bge_retrieval_profile bge_retrieval_profile.cpp)
    target_link_libraries(bge_retrieval_profile PRIVATE edge_voice_rag_core)
endif()
