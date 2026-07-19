#pragma once

#include <string>

class RagResponseParser {
public:
    static std::string extractAnswerOrRaw(const std::string& response);

    static std::string extractRagAnswerOrRaw(const std::string& response);
};