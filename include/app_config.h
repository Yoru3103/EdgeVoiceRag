#pragma once

#include <string>

class AppConfig {
public:
    explicit AppConfig(const std::string& config_path);

    bool load();

    const std::string& knowledgePath() const;
    int topK() const;

    const std::string& ragBackend() const;
    const std::string& ragEndpoint() const;

private:
    std::string config_path_;

    std::string knowledge_path_;
    int top_k_;

    std::string rag_backend_;
    std::string rag_endpoint_;

    static std::string trim(const std::string& text);
};