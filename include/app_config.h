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
    int ragTimeoutMs() const;

    const std::string& llmEndpoint() const;
    int llmTimeoutMs() const;

private:
    std::string config_path_;

    std::string knowledge_path_;
    int top_k_;

    std::string rag_backend_;
    std::string rag_endpoint_;
    int rag_timeout_ms_;

    std::string llm_endpoint_;
    int llm_timeout_ms_;

    static std::string trim(const std::string& text);
    static int parsePositiveInt(
        const std::string& value,
        int default_value
    );
};
