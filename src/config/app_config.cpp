#include "app_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

AppConfig::AppConfig(const std::string& config_path)
    : config_path_(config_path)
    , knowledge_path_("docs/vehicle_manual.txt")
    , top_k_(3)
    , rag_backend_("local")
    , rag_endpoint_("tcp://localhost:5555")
    , rag_timeout_ms_(3000)
    , llm_endpoint_("tcp://localhost:8899")
    , llm_timeout_ms_(30000) {
}

bool AppConfig::load() {
    std::ifstream file(config_path_);

    if (!file.is_open()) {
        return false;
    }

    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);

        if (line.empty() || line[0] == '#') {
            continue;
        }

        const std::size_t separator = line.find('=');

        if (separator == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, separator));

        const std::string value = trim(line.substr(separator + 1));

        if (key == "knowledge_path") {
            knowledge_path_ = value;
        } else if (key == "top_k") {
            top_k_ = parsePositiveInt(
                value,
                3
            );
        } else if (key == "rag_backend") {
            if (
                value == "local"
                || value == "zmq"
                || value == "python_zmq"
            ) {
                rag_backend_ = value;
            }
        } else if (key == "rag_endpoint") {
            rag_endpoint_ = value;
        } else if (key == "rag_timeout_ms") {
            rag_timeout_ms_ = parsePositiveInt(
                value,
                3000
            );
        } else if (key == "llm_endpoint") {
            llm_endpoint_ = value;
        } else if (key == "llm_timeout_ms") {
            llm_timeout_ms_ = parsePositiveInt(
                value,
                30000
            );
        }
    }

    return true;
}

const std::string& AppConfig::knowledgePath() const {
    return knowledge_path_;
}

int AppConfig::topK() const {
    return top_k_;
}

const std::string& AppConfig::ragBackend() const {
    return rag_backend_;
}

const std::string& AppConfig::ragEndpoint() const {
    return rag_endpoint_;
}

int AppConfig::ragTimeoutMs() const {
    return rag_timeout_ms_;
}

const std::string& AppConfig::llmEndpoint() const {
    return llm_endpoint_;
}

int AppConfig::llmTimeoutMs() const {
    return llm_timeout_ms_;
}

std::string AppConfig::trim(
    const std::string& text
) {
    const auto begin = std::find_if_not(
        text.begin(),
        text.end(),
        [](unsigned char character) {
            return std::isspace(character);
        }
    );

    const auto end = std::find_if_not(
        text.rbegin(),
        text.rend(),
        [](unsigned char character) {
            return std::isspace(character);
        }
    ).base();

    if (begin >= end) {
        return "";
    }

    return std::string(begin, end);
}

int AppConfig::parsePositiveInt(
    const std::string& value,
    int default_value
) {
    try {
        const int parsed = std::stoi(value);

        if (parsed > 0) {
            return parsed;
        }
    } catch (...) {
    }

    return default_value;
}
