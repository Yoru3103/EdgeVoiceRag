#include "board_app_config.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");

    if (first == std::string::npos) {
        return "";
    }

    const std::size_t last = text.find_last_not_of(" \t\r\n");

    return text.substr(first, last - first + 1);
}

using ConfigMap = std::unordered_map<std::string, std::string>;

ConfigMap loadValues(const std::string& path) {
    std::ifstream input(path);

    if (!input.is_open()) {
        throw std::runtime_error("failed to open board config: " + path);
    }

    ConfigMap values;

    std::string line;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        line_number++;

        line = trim(line);

        if (line.empty() || line.front() == '#') {
            continue;
        }

        const std::size_t separator = line.find('=');

        if (separator == std::string::npos) {
            throw std::runtime_error(
                "invalid config line " + std::to_string(line_number)
            );
        }

        const std::string key = line.substr(0, separator);

        const std::string value = line.substr(separator + 1);

        if (key.empty()) {
            throw std::runtime_error(
                "empty config key at line " + std::to_string(line_number)
            );
        }

        values[key] = value;
    }

    return values;
}

bool parseBoolean(const std::string& text, const std::string& key) {
    if (text == "true" || text == "1") {
        return true;
    }

    if (text == "false" || text == "0") {
        return false;
    }

    throw std::runtime_error(
        "invalid boolean config value for "
            + key
            + ": "
            + text
    );
}

bool getBoolean(const ConfigMap& values, const std::string& key, bool default_value) {
    const auto item = values.find(key);

    if (item == values.end()) {
        return default_value;
    }

    return parseBoolean(
        item->second,
        key
    );
}

std::string getString(
    const ConfigMap& values,
    const std::string& key,
    const std::string& default_value
) {
    const auto item = values.find(key);

    if (item == values.end()) {
        return default_value;
    }

    return item->second;
}

template <typename Number>
Number parseNumber(const std::string& text, const std::string& key) {
    std::istringstream input(text);

    Number value{};
    input >> value;

    if (!input || !input.eof()) {
        throw std::runtime_error(
            "invalid numeric config value for "
                + key
                + ": "
                + text
        );
    }

    return value;
}

template <typename Number>
Number getNumber(
    const ConfigMap& values,
    const std::string& key,
    Number default_value
) {
    const auto item = values.find(key);

    if (item == values.end()) {
        return default_value;
    }

    return parseNumber<Number>(
        item->second,
        key
    );
}

}   // namespace

BoardAppConfig BoardAppConfig::load(const std::string& path) {
    const ConfigMap values = loadValues(path);

    BoardAppConfig config;

    config.knowledge_path = getString(
        values,
        "knowledge_path",
        config.knowledge_path
    );

    config.top_k = getNumber<int>(
        values,
        "top_k",
        config.top_k
    );

    config.retrieval_backend = getString(
        values,
        "retrieval_backend",
        config.retrieval_backend
    );

    config.bge_model_path = getString(
        values,
        "bge_model_path",
        config.bge_model_path
    );

    config.bge_tokenizer_path = getString(
        values,
        "bge_tokenizer_path",
        config.bge_tokenizer_path
    );

    config.dense_index_metadata_path = getString(
        values,
        "dense_index_metadata_path",
        config.dense_index_metadata_path
    );

    config.dense_embeddings_path = getString(
        values,
        "dense_embeddings_path",
        config.dense_embeddings_path
    );

    config.dense_minimum_similarity =
        getNumber<float>(
            values,
            "dense_minimum_similarity",
            config.dense_minimum_similarity
        );

    config.hybrid_rrf_k =
        getNumber<float>(
            values,
            "hybrid_rrf_k",
            config.hybrid_rrf_k
        );

    config.hybrid_sparse_weight =
        getNumber<float>(
            values,
            "hybrid_sparse_weight",
            config.hybrid_sparse_weight
        );

    config.hybrid_dense_weight =
        getNumber<float>(
            values,
            "hybrid_dense_weight",
            config.hybrid_dense_weight
        );

    config.hybrid_candidate_top_k =
        getNumber<int>(
            values,
            "hybrid_candidate_top_k",
            config.hybrid_candidate_top_k
        );

    config.relevance_filter_enabled =
            getBoolean(
                values,
                "relevance_filter_enabled",
                config.relevance_filter_enabled
            );

        config.relevance_minimum_sparse_score =
            getNumber<float>(
                values,
                "relevance_minimum_sparse_score",
                config.relevance_minimum_sparse_score
            );

        config.relevance_minimum_dense_similarity =
            getNumber<float>(
                values,
                "relevance_minimum_dense_similarity",
                config.relevance_minimum_dense_similarity
            );

        config.relevance_candidate_top_k =
            getNumber<int>(
                values,
                "relevance_candidate_top_k",
                config.relevance_candidate_top_k
            );

    config.capture_device = getString(
        values,
        "capture_device",
        config.capture_device
    );

    config.playback_device = getString(
        values,
        "playback_device",
        config.playback_device
    );

    config.asr_model_path = getString(
        values,
        "asr_model_path",
        ""
    );

    config.asr_tokens_path = getString(
        values,
        "asr_tokens_path",
        ""
    );

    config.vad_model_path = getString(
        values,
        "vad_model_path",
        ""
    );

    config.tts_model_dir = getString(
        values,
        "tts_model_dir",
        ""
    );

    config.llm_backend = getString(
        values,
        "llm_backend",
        config.llm_backend
    );

    config.llm_model_path = getString(
        values,
        "llm_model_path",
        ""
    );

    config.asr_num_threads = getNumber<int>(
        values,
        "asr_num_threads",
        config.asr_num_threads
    );

    config.vad_num_threads = getNumber<int>(
        values,
        "vad_num_threads",
        config.vad_num_threads
    );

    config.tts_num_threads = getNumber<int>(
        values,
        "tts_num_threads",
        config.tts_num_threads
    );

    config.llm_max_new_tokens =
        getNumber<int>(
            values,
            "llm_max_new_tokens",
            config.llm_max_new_tokens
        );

    config.llm_max_context_len =
        getNumber<int>(
            values,
            "llm_max_context_len",
            config.llm_max_context_len
        );

    config.normal_vad_threshold =
        getNumber<float>(
            values,
            "normal_vad_threshold",
            config.normal_vad_threshold
        );

    config.barge_vad_threshold =
        getNumber<float>(
            values,
            "barge_vad_threshold",
            config.barge_vad_threshold
        );

    config.normal_silence_seconds =
        getNumber<float>(
            values,
            "normal_silence_seconds",
            config.normal_silence_seconds
        );

    config.barge_silence_seconds =
        getNumber<float>(
            values,
            "barge_silence_seconds",
            config.barge_silence_seconds
        );

    config.max_turns =
        getNumber<std::size_t>(
            values,
            "max_turns",
            config.max_turns
        );

    config.validate();

    return config;
}

void BoardAppConfig::validate() const {
    if (knowledge_path.empty()) {
        throw std::invalid_argument(
            "knowledge_path must not be empty"
        );
    }

    if (top_k <= 0) {
        throw std::invalid_argument(
            "top_k must be greater than zero"
        );
    }

    if (
        retrieval_backend != "bm25" &&
        retrieval_backend != "dense" &&
        retrieval_backend != "hybrid"
    ) {
        throw std::invalid_argument(
            "retrieval_backend must be "
            "bm25, dense or hybrid"
        );
    }

    if (
        retrieval_backend == "dense"
        || retrieval_backend == "hybrid"
    ) {
        if (
            bge_model_path.empty()
            || bge_tokenizer_path.empty()
            || dense_index_metadata_path.empty()
            || dense_embeddings_path.empty()
        ) {
            throw std::invalid_argument(
                "dense retrieval paths must not be empty"
            );
        }
    }

    if (
        dense_minimum_similarity < -1.0F
        || dense_minimum_similarity > 1.0F
    ) {
        throw std::invalid_argument(
            "dense_minimum_similarity must be "
            "between -1 and 1"
        );
    }

    if (hybrid_rrf_k <= 0.0F) {
        throw std::invalid_argument(
            "hybrid_rrf_k must be greater than zero"
        );
    }

    if (
        hybrid_sparse_weight < 0.0F
        || hybrid_dense_weight < 0.0F
    ) {
        throw std::invalid_argument(
            "hybrid weights must be non-negative"
        );
    }

    if (
        hybrid_sparse_weight == 0.0F
        && hybrid_dense_weight == 0.0F
    ) {
        throw std::invalid_argument(
            "hybrid weights cannot both be zero"
        );
    }

    if (hybrid_candidate_top_k <= 0) {
        throw std::invalid_argument(
            "hybrid_candidate_top_k must be "
            "greater than zero"
        );
    }

    if (
        !std::isfinite(
            relevance_minimum_sparse_score
        )
        || relevance_minimum_sparse_score <= 0.0F
    ) {
        throw std::invalid_argument(
            "relevance_minimum_sparse_score "
            "must be finite and greater than zero"
        );
    }

    if (
        !std::isfinite(
            relevance_minimum_dense_similarity
        )
        || relevance_minimum_dense_similarity <= 0.0F
        || relevance_minimum_dense_similarity >= 1.0F
    ) {
        throw std::invalid_argument(
            "relevance_minimum_dense_similarity "
            "must be finite and between zero and one"
        );
    }

    if (relevance_candidate_top_k <= 0) {
        throw std::invalid_argument(
            "relevance_candidate_top_k "
            "must be greater than zero"
        );
    }   

    if (
        capture_device.empty()
        || playback_device.empty()
    ) {
        throw std::invalid_argument(
            "ALSA devices must not be empty"
        );
    }

    if (
        asr_model_path.empty()
        || asr_tokens_path.empty()
    ) {
        throw std::invalid_argument(
            "ASR model paths must not be empty"
        );
    }

    if (vad_model_path.empty()) {
        throw std::invalid_argument(
            "VAD model path must not be empty"
        );
    }

    if (tts_model_dir.empty()) {
        throw std::invalid_argument(
            "TTS model directory must not be empty"
        );
    }

    if (
        llm_backend != "mock"
        && llm_backend != "rkllm"
    ) {
        throw std::invalid_argument(
            "llm_backend must be mock or rkllm"
        );
    }

    if (
        llm_backend == "rkllm"
        && llm_model_path.empty()
    ) {
        throw std::invalid_argument(
            "RKLLM model path must not be empty"
        );
    }

    if (
        asr_num_threads <= 0
        || vad_num_threads <= 0
        || tts_num_threads <= 0
    ) {
        throw std::invalid_argument(
            "inference thread counts must be positive"
        );
    }

    const auto valid_threshold =
        [](float value) {
            return value > 0.0F
                && value < 1.0F;
        };

    if (
        !valid_threshold(normal_vad_threshold)
        || !valid_threshold(barge_vad_threshold)
    ) {
        throw std::invalid_argument(
            "VAD thresholds must be between "
            "zero and one"
        );
    }
}
