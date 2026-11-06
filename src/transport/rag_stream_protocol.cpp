#include "rag_stream_protocol.h"

#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace {

constexpr const char* kQueryType = "rag_query";
constexpr const char* kChunkType = "rag_chunk";
constexpr const char* kFinishedType = "rag_finished";
constexpr const char* kErrorType = "rag_error";

using Json = nlohmann::json;

void requireObject(const Json& message) {
    if (!message.is_object()) {
        throw std::runtime_error("RAG protocol message must be a JSON object");
    }
}

void requireVersion(const Json& message) {
    if (
        !message.contains("version") ||
        !message["version"].is_number_integer() ||
        message["version"].get<int>() != RagStreamProtocol::KVersion
    ) {
        throw std::runtime_error("unsupported RAG protocol version");
    }
}

std::string requireNonEmptyString(const Json& message, const char* field) {
    if (
        !message.contains(field) ||
        !message[field].is_string()
    ) {
        throw std::runtime_error(
            std::string("missing or invalid RAG field: ") + field
        );
    }

    const std::string value = message[field].get<std::string>();

    if (value.empty()) {
        throw std::runtime_error(std::string("RAG field must not be empty: ") + field);
    }

    return value;
}

std::string optionalString(const Json& message, const char* field) {
    if (!message.contains(field)) {
        throw std::runtime_error(std::string("invalid RAG string field: ") + field);
    }

    return message[field].get<std::string>();
}

bool requireBoolean(const Json& message, const char* field) {
    if (
        !message.contains(field) ||
        !message[field].is_boolean()
    ) {
        throw std::runtime_error(std::string("missing or invalid RAG boolean field: ") + field);
    }

    return message[field].get<bool>();
}

std::size_t requireSequence(const Json& message) {
    if (
        !message.contains("sequence") ||
        !message["sequence"].is_number_unsigned()
    ) {
        throw std::runtime_error("RAG sequence must be a non-negative integer");
    }

    return message["sequence"].get<std::size_t>();
}

double requireElapsedMilliseconds(const Json& message) {
    if (
        !message.contains("elapsed_ms") ||
        !message["elapsed_ms"].is_number()
    ) {
        throw std::runtime_error("RAG elapsed_ms must be numeric");
    }

    const double elapsed = message["elapsed_ms"].get<double>();

    if (elapsed < 0.0) {
        throw std::runtime_error("RAG elapsed_ms must not be negative");
    }

    return elapsed;
}

RagStreamEventType eventTypeFromString(const std::string& type) {
    if (type == kChunkType) {
        return RagStreamEventType::Chunk;
    }

    if (type == kFinishedType) {
        return RagStreamEventType::Finished;
    }

    if (type == kErrorType) {
        return RagStreamEventType::Error;
    }

    throw std::runtime_error("unsupported RAG stream event type: " + type);
}

void validateEvent(const RagStreamEvent& event) {
    switch (event.type) {
        case RagStreamEventType::Chunk:
            if (
                !event.ok ||
                event.delta.empty() ||
                event.finished
            ) {
                throw std::runtime_error("invalid RAG chunk event");
            }
            return;

        case RagStreamEventType::Finished:
            if (
                !event.ok ||
                event.answer.empty() ||
                !event.finished
            ) {
                throw std::runtime_error("invalid RAG finished event");
            }
            return;

        case RagStreamEventType::Error:
            if (
                event.ok ||
                event.error.empty() ||
                !event.finished
            ) {
                throw std::runtime_error("invalid RAG error event");
            }
            return;
    }

    throw std::runtime_error("invalid RAG stream event");
}

}   // namespace

std::string RagStreamProtocol::encodeRequest(const RagStreamRequest& request) {
    if (request.request_id.empty()) {
        throw std::invalid_argument("RAG request_id must not be empty");
    }
    if (request.query.empty()) {
        throw std::invalid_argument("RAG query must not be empty");
    }

    const Json message = {
        {"version", KVersion},
        {"type", kQueryType},
        {"request_id", request.request_id},
        {"query", request.query},
        {"stream", true}
    };

    return message.dump();
}

RagStreamRequest RagStreamProtocol::decodeRequest(const std::string& message) {
    const Json json = Json::parse(message);

    requireObject(json);
    requireVersion(json);

    if (
        !json.contains("type") ||
        !json["type"].is_string() ||
        json["type"].get<std::string>() != kQueryType
    ) {
        throw std::runtime_error("unsupported RAG request type");
    }

    if (
        !json.contains("stream") ||
        !json["stream"].is_boolean() ||
        !json["stream"].get<bool>()
    ) {
        throw std::runtime_error("RAG stream request must set stream=true");
    }

    RagStreamRequest request;

    request.request_id = requireNonEmptyString(json, "request_id");
    request.query = requireNonEmptyString(json, "query");

    return request;
}

std::string RagStreamProtocol::encodeEvent(const RagStreamEvent& event) {
    validateEvent(event);

    if (event.request_id.empty()) {
        throw std::invalid_argument("RAG event request_id must not be empty");
    }
    if (event.elapsed_ms < 0.0) {
        throw std::invalid_argument("RAG event elapsed_ms must not be negative");
    }

    const Json message = {
        {"version", KVersion},
        {"type", eventTypeToString(event.type)},
        {"ok", event.ok},
        {"request_id", event.request_id},
        {"sequence", event.sequence},
        {"delta", event.delta},
        {"answer", event.answer},
        {"backend", event.backend},
        {"llm_backend", event.llm_backend},
        {"error", event.error},
        {"elapsed_ms", event.elapsed_ms},
        {"finished", event.finished}
    };

    return message.dump();
}

RagStreamEvent RagStreamProtocol::decodeEvent(const std::string& message) {
    const Json json = Json::parse(message);

    requireObject(json);
    requireVersion(json);

    const std::string type = requireNonEmptyString(json, "type");

    RagStreamEvent event;

    event.type = eventTypeFromString(type);
    event.ok = requireBoolean(json, "ok");

    event.request_id = requireNonEmptyString(json, "request_id");

    event.sequence = requireSequence(json);

    event.delta = optionalString(json, "delta");
    event.answer = optionalString(json, "answer");

    event.backend = optionalString(json, "backend");
    event.llm_backend = optionalString(json, "llm_backend");

    event.error = optionalString(json, "error");

    event.elapsed_ms = requireElapsedMilliseconds(json);

    event.finished = requireBoolean(json, "finished");

    validateEvent(event);

    return event;
}

std::string RagStreamProtocol::eventTypeToString(RagStreamEventType type) {
    switch (type) {
        case RagStreamEventType::Chunk:
            return kChunkType;

        case RagStreamEventType::Finished:
            return kFinishedType;

        case RagStreamEventType::Error:
            return kErrorType;
    }

    throw std::runtime_error("unsupported RAG stream event type");
}
