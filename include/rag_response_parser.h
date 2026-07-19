#pragma once

#include <string>

// 解析远端json
class RagResponseParser {
public:
    // LLM路径，优先检索generated_answer
    static std::string extractAnswerOrRaw(const std::string& response);

    // RAG路径，只优先answer
    static std::string extractRagAnswerOrRaw(const std::string& response);
};