#pragma once

#include <string>

class AppConfig {
public:
    explicit AppConfig(const std::string& config_path);

    bool load();

    const std::string& knowledgePath() const;
    int topK() const;

private:
    std::string config_path_;

    std::string knowledge_path_;
    int top_k_;

    static std::string trim(const std::string& text);
};