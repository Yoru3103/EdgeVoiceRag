#pragma once

#include <string>

struct LlmRequest {
    std::string request_id;
    std::string prompt;
    bool stream = false;
};

struct LlmResponse {
    bool ok = false;
    std::string request_id;
    std::string answer;
    std::string backend;
    std::string error;
    double elapsed_ms = 0.0;
    bool finished = true;
};

class LlmProtocol {
public:
    static std::string encodeRequest(const LlmRequest& request);
    static LlmRequest decodeRequest(const std::string& message);

    static std::string encodeResponse(const LlmResponse& response);
    static LlmResponse decodeResponse(const std::string& message);
};