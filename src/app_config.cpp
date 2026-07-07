#include "app_config.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

AppConfig::AppConfig(const std::string& config_path)
    : config_path_(config_path)
    , knowledge_path_("docs/vehicle_manual.txt")
    , top_k_(3)
    , rag_backend_("local")
    , rag_endpoint_("tcp://localhost:5555")
    , rag_timeout_ms_(3000) {
}

bool AppConfig::load() {
    std::ifstream file(config_path_);

    if (!file.is_open()) {
        return false;
    }

    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);

        if (line.empty()) {
            continue;
        }

        if (line[0] == '#') {
            continue;
        }

        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));

        if (key == "knowledge_path") {
            knowledge_path_ = value;
        } else if (key == "top_k") {
            try {
                top_k_ = std::stoi(value);
            } catch (...) {
                top_k_ = 3;
            }

            if (top_k_ < 0) {
                top_k_ = 3;
            }
        } else if (key == "rag_backend") {
            rag_backend_ = value;

            if (rag_backend_ != "local" && rag_backend_ != "zmq") {
                rag_backend_ = "local";
            }
        } else if (key == "rag_endpoint") {
            rag_endpoint_ = value;
        } else if (key == "rag_timeout_ms") {
            try {
                rag_timeout_ms_ = std::stoi(value);
            } catch (...) {
                rag_timeout_ms_ = 3000;
            }

            if (rag_timeout_ms_ < 0) {
                rag_timeout_ms_ = 3000;
            }
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

std::string AppConfig::trim(const std::string& text) {
    const auto begin = std::find_if_not(
        text.begin(), 
        text.end(), 
        [](unsigned char ch) {
            return std::isspace(ch);
        }
    );

    const auto end = std::find_if_not(
        text.rbegin(),
        text.rend(),
        [](unsigned char ch) {
            return std::isspace(ch);
        }
    ).base();       // bass将反向迭代器返回正向迭代器，而base变回正向迭代器时会指向原本位置的后一个位置，恰好满足左闭右开区间

    if (begin >= end) {
        return "";
    }

    return std::string(begin, end);
}