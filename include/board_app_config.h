#pragma once

#include <cstddef>
#include <string>

struct BoardAppConfig {
    std::string knowledge_path = "vector_db/chunks.json";

    int top_k = 3;

    std::string retrieval_backend = "bm25";
    std::string bge_model_path = "models/embedding/bge-small-zh-v1.5/model.onnx";
    std::string bge_tokenizer_path = "models/embedding/bge-small-zh-v1.5/tokenizer.json";
    std::string dense_index_metadata_path = "vector_db/bge-small-zh-v1.5/index_meta.json";
    std::string dense_embeddings_path = "vector_db/bge-small-zh-v1.5/embeddings.f32";

    float dense_minimum_similarity = -1.0F;

    float hybrid_rrf_k = 60.0F;
    float hybrid_sparse_weight = 1.0F;
    float hybrid_dense_weight = 1.0F;

    int hybrid_candidate_top_k = 20;

    bool relevance_filter_enabled = true;

    float relevance_minimum_sparse_score = 6.0F;
    float relevance_minimum_dense_similarity = 0.40F;

    int relevance_candidate_top_k = 20;

    float response_direct_minimum_sparse_score = 8.0F;
    float response_direct_minimum_dense_similarity = 0.55F;

    std::string capture_device = "plughw:CARD=rockchipes8388,DEV=0";
    std::string playback_device = "plughw:CARD=rockchipes8388,DEV=0";

    std::string asr_model_path;
    std::string asr_tokens_path;
    std::string vad_model_path;
    std::string tts_model_dir;

    std::string llm_backend = "mock";
    std::string llm_model_path;

    std::string agent_planner = "auto";
    std::size_t agent_max_steps = 4;

    int agent_confirmation_timeout_ms = 30000;

    std::string agent_device_backend = "mock";

    std::string agent_iio_root = "/sys/bus/iio/devices";

    std::string agent_iio_device_name = "edge_dht11";

    float agent_mock_temperature_c = 28.5F;
    float agent_mock_humidity_percent = 60.0F;

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
