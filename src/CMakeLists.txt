# A single compatibility target; each module owns its explicit source list.
add_library(edge_voice_rag_core)
target_include_directories(edge_voice_rag_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(edge_voice_rag_core
    PUBLIC zmq nlohmann_json::nlohmann_json Threads::Threads
)
set_target_properties(edge_voice_rag_core PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)

add_subdirectory(common)
add_subdirectory(config)
add_subdirectory(audio)
add_subdirectory(speech)
add_subdirectory(retrieval)
add_subdirectory(inference)
add_subdirectory(response)
add_subdirectory(transport)
add_subdirectory(devices)
add_subdirectory(agent)
add_subdirectory(voice)
add_subdirectory(observability)

add_subdirectory(apps)
