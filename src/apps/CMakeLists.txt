add_executable(edge_voice_rag main.cpp)
target_link_libraries(edge_voice_rag PRIVATE edge_voice_rag_core)

foreach(app IN ITEMS rag_server llm_server agent_demo)
    add_executable(${app} ${app}.cpp)
    target_link_libraries(${app} PRIVATE edge_voice_rag_core)
endforeach()

foreach(app IN ITEMS zmq_rep_server zmq_req_client rag_client)
    add_executable(${app} ${app}.cpp)
    target_link_libraries(${app} PRIVATE zmq)
endforeach()

if(EDGE_ENABLE_SHERPA_ASR)
    add_executable(sherpa_asr_demo sherpa_asr_demo.cpp)
    target_link_libraries(sherpa_asr_demo PRIVATE edge_voice_rag_core)
endif()

if(EDGE_ENABLE_ALSA AND EDGE_ENABLE_SHERPA_ASR
   AND EDGE_ENABLE_SHERPA_VAD AND EDGE_ENABLE_SHERPA_TTS)
    add_executable(board_voice_assistant board_voice_main.cpp)
    target_link_libraries(board_voice_assistant PRIVATE edge_voice_rag_core)
endif()

# Standalone board output probe; tone mode needs ALSA only.
if(EDGE_ENABLE_ALSA)
    add_executable(audio_output_check audio_output_check.cpp)
    target_link_libraries(audio_output_check PRIVATE edge_voice_rag_core)
endif()

if(EDGE_ENABLE_SHERPA_TTS)
    add_executable(tts_benchmark tts_benchmark.cpp)
    target_link_libraries(tts_benchmark PRIVATE edge_voice_rag_core)
endif()
