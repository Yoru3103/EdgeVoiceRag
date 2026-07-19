#include "llm_protocol.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

namespace {
    constexpr int kProtocolVersion = 1;
    constexpr const char* kGenerateType = "generate";

} // namespace

std::string LlmProtocol::encodeRequest(const LlmRequest& request) {
    if (request.request_id.empty()) {
        throw std::invalid_argument("LLM request_id cannot be empty");
    }

    if (request.prompt.empty()) {
        throw std::invalid_argument("LLM prompt cannot be empty");
    }

    const nlohmann::json message = {
        {"version", kProtocolVersion},
        {"type", kGenerateType},
        {"request_id", request.request_id},
        {"prompt", request.prompt},
        {"stream", request.stream}
    };

    return message.dump();
}

LlmRequest LlmProtocol::decodeRequest(const std::string& message) {
    const auto json = nlohmann::json::parse(message);

    if (json.value("version", 0) != kProtocolVersion) {
        throw std::runtime_error("unsupported LLM protocol version");
    }

    if (json.value("type", "") != kGenerateType) {
        throw std::runtime_error("unsupported LLM request type");
    }

    LlmRequest request;

    request.request_id = json.value("request_id", "");
    request.prompt = json.value("prompt", "");
    request.stream = json.value("stream", false);

    if (request.request_id.empty()) {
        throw std::runtime_error("missing LLM request_id");
    }

    if (request.prompt.empty()) {
        throw std::runtime_error("missing LLM prompt");
    }

    return request;
}

std::string LlmProtocol::encodeResponse(const LlmResponse& response) {
    const nlohmann::json message = {
        {"version", kProtocolVersion},
        {"type", "generation_result"},
        {"ok", response.ok},
        {"request_id", response.request_id},
        {"answer", response.answer},
        {"backend", response.backend},
        {"error", response.error},
        {"elapsed_ms", response.elapsed_ms},
        {"finished", response.finished}
    };

    return message.dump();
}

LlmResponse LlmProtocol::decodeResponse(const std::string& message) {
    const auto json = nlohmann::json::parse(message);

    if (json.value("version", 0) != kProtocolVersion) {
        throw std::runtime_error("unsupported LLM protocol version");
    }

    if (json.value("type", "") != "generation_result") {
        throw std::runtime_error("unsupported LLM response type");
    }

    LlmResponse response;

    response.ok = json.value("ok", false);
    response.request_id = json.value("request_id", "");
    response.answer = json.value("answer", "");
    response.backend = json.value("backend", "");
    response.error = json.value("error", "");
    response.elapsed_ms = json.value("elapsed_ms", 0.0);
    response.finished = json.value("finished", true);

    if (response.request_id.empty()) {
        throw std::runtime_error("missing LLM response request_id");
    }

    if (response.ok && response.answer.empty()) {
        throw std::runtime_error("successful LLM response has empty answer");
    }

    return response;
}
