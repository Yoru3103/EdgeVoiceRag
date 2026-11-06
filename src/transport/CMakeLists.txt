target_sources(edge_voice_rag_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/rag_client_zmq.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rag_response_parser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/zmq_text_client.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rag_stream_client.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/llm_protocol.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/llm_service.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/llm_stream_service.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/llm_control_server.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rag_stream_protocol.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rag_control_protocol.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rag_control_client.cpp"
)
