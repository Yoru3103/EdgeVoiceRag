#include "llm_protocol.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

namespace {

constexpr int kProtocolVersion = 1;
constexpr const char* kGenerateType = "generate";
constexpr const char* kChunkType = "generation_chunk";
constexpr const char* kFinishedType = "generation_finished";
constexpr const char* kErrorType = "generation_error";

std::string streamTypeToString(LlmStreamEventType type) {
    switch (type) {
        case LlmStreamEventType::Chunk:
            return kChunkType;

        case LlmStreamEventType::Finished:
            return kFinishedType;

        case LlmStreamEventType::Error:
            return kErrorType;
    }

    throw std::runtime_error(
        "unsupported LLM stream event type"
    );
}

LlmStreamEventType streamTypeFromString(const std::string& type) {
    if (type == kChunkType) {
        return LlmStreamEventType::Chunk;
    }

    if (type == kFinishedType) {
        return LlmStreamEventType::Finished;
    }

    if (type == kErrorType) {
        return LlmStreamEventType::Error;
    }

    throw std::runtime_error(
        "unsupported LLM stream event type: " + type
    );
}

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

std::string LlmProtocol::encodeStreamEvent(const LlmStreamEvent& event) {
    if (event.request_id.empty()) {
        throw std::invalid_argument("LLM stream request_id cannot be empty");
    }

    if (event.type == LlmStreamEventType::Chunk && event.delta.empty()) {
        throw std::invalid_argument("LLM stream chunk cannot be empty");
    }

    if (event.type == LlmStreamEventType::Finished && event.answer.empty()) {
        throw std::invalid_argument("finished LLM answer cannot be empty");
    }

    if (event.type == LlmStreamEventType::Error && event.error.empty()) {
        throw std::invalid_argument("LLM stream error cannot be empty");
    }

    const nlohmann::json message = {
        {"version", kProtocolVersion},
        {"type", streamTypeToString(event.type)},
        {"ok", event.ok},
        {"request_id", event.request_id},
        {"delta", event.delta},
        {"answer", event.answer},
        {"backend", event.backend},
        {"error", event.error},
        {"sequence", event.sequence},
        {"elapsed_ms", event.elapsed_ms},
        {"finished", event.finished}
    };

    return message.dump();
}

LlmStreamEvent LlmProtocol::decodeStreamEvent(const std::string& message) {
    const auto json = nlohmann::json::parse(message);

    if (json.value("version", 0) != kProtocolVersion) {
        throw std::runtime_error("unsupported LLM protocol version");
    }

    LlmStreamEvent event;

    event.type = streamTypeFromString(json.value("type", ""));
    event.ok = json.value("ok", false);
    event.request_id = json.value("request_id", "");
    event.delta = json.value("delta", "");
    event.answer = json.value("answer", "");
    event.backend = json.value("backend", "");
    event.error = json.value("error", "");
    event.sequence = json.value("sequence", std::size_t{0});
    event.elapsed_ms = json.value("elapsed_ms", 0.0);
    event.finished = json.value("finished", false);

    if (event.request_id.empty()) {
        throw std::runtime_error("missing LLM stream request_id");
    }

    if (event.type == LlmStreamEventType::Chunk &&
        (!event.ok || event.delta.empty() || event.finished)
    ) {
        throw std::runtime_error("invalid LLM stream chunk");
    }

    if (event.type == LlmStreamEventType::Finished &&
        (!event.ok || event.answer.empty() || !event.finished)
    ) {
        throw std::runtime_error("invalid LLM finished event");
    }

    if (event.type == LlmStreamEventType::Error &&
        (event.ok || event.error.empty() || !event.finished)
    ) {
        throw std::runtime_error("invalid LLM error event");
    }

    return event;
}
