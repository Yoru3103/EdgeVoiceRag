foreach(tool IN ITEMS retrieval_benchmark voice_performance_summary)
    add_executable(${tool} ${tool}.cpp)
    target_link_libraries(${tool} PRIVATE edge_voice_rag_core)
endforeach()
