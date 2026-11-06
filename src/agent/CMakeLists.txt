target_sources(edge_voice_rag_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tool_registry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vehicle_tools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rule_agent_planner.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/agent_executor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/llm_agent_planner.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/agent_answer_backend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/agent_intent_classifier.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/agent_routing_answer_backend.cpp"
)
