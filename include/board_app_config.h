#pragma once

#include <cstddef>
#include <string>

struct BoardAppConfig {
    std::string knowledge_path = "docs/vehicle_manual.txt";

    int top_k = 3;

    std::string capture_device = "plughw:CARD=rockchipes8388,DEV=0";
    std::string playback_device = "plughw:CARD=rockchipes8388,DEV=0";

    std::string asr_model_path;
    std::string asr_tokens_path;
    std::string vad_model_path;
    std::string tts_model_dir;

    std::string llm_backend = "mock";
    std::string llm_model_path;

    int asr_num_threads = 2;
    int vad_num_threads = 1;
    int tts_num_threads = 2;

    int llm_max_new_tokens = 256;
    int llm_max_context_len = 2048;

    float normal_vad_threshold = 0.25F;
    float barge_vad_threshold = 0.60F;

    float normal_silence_seconds = 0.8F;
    float barge_silence_seconds = 0.5F;

    std::size_t max_turns = 0;

    static BoardAppConfig load(const std::string& path);

    void validate() const;
};
