#include "rag_control_protocol.h"

#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace {

using Json = nlohmann::json;

constexpr const char* KCancelType = "cancel";
constexpr const char* KCancelResultType = "cancel_result";

void requireObject(const Json& json) {
    if (!json.is_object()) {
        throw std::runtime_error(
            "RAG control message must be "
            "a JSON object"
        );
    }
}

void requireVersion(const Json& json) {
    if (
        !json.contains("version") ||
        !json["version"].is_number_integer() ||
        json["version"].get<int>() != RagControlProtocol::KVersion
    ) {
        throw std::runtime_error(
            "unsupported RAG control "
            "protocol version"
        );
    }
}

std::string requireString(
    const Json& json,
    const char* field,
    bool allow_empty
) {
    if (
        !json.contains(field) ||
        !json[field].is_string()
    ) {
        throw std::runtime_error(
            std::string(
                "missing or invalid RAG "
                "control field: "
            ) + field
        );
    }

    const std::string value = json[field].get<std::string>();

    if (!allow_empty && value.empty()) {
        throw std::runtime_error(
            std::string(
                "RAG control field must not "
                "be empty: "
            ) + field
        );
    }

    return value;
}

bool requireBoolean(
    const Json& json,
    const char* field
) {
    if (
        !json.contains(field) ||
        !json[field].is_boolean()
    ) {
        throw std::runtime_error(
            std::string(
                "missing or invalid RAG "
                "control boolean field: "
            ) +
            field
        );
    }

    return json[field].get<bool>();
}

bool containsNonWhitespace(const std::string& value) {
    return value.find_first_not_of(" \t\n\r") != std::string::npos;
}

void validateRequestId(const std::string& request_id) {
    if (
        request_id.empty() ||
        !containsNonWhitespace(request_id)
    ) {
        throw std::invalid_argument(
            "RAG cancel request_id must not "
            "be empty"
        );
    }
}

void validateResponse(const RagCancelResponse& response) {
    if (response.ok && response.request_id.empty()) {
        throw std::runtime_error(
            "successful RAG cancel response "
            "has empty request_id"
        );
    }

    if (!response.ok && response.cancelled) {
        throw std::runtime_error(
            "failed RAG cancel response "
            "cannot set cancelled=true"
        );
    }

    if (!response.ok && response.error.empty()) {
        throw std::runtime_error(
            "failed RAG cancel response "
            "has empty error"
        );
    }
}

}   // namespace

std::string RagControlProtocol::encodeRequest(const RagCancelRequest& request) {
    validateRequestId(request.request_id);

    const Json json = {
        {"version", KVersion},
        {"type", KCancelType},
        {"request_id", request.request_id}
    };

    return json.dump();
}

RagCancelRequest RagControlProtocol::decodeRequest(const std::string& message) {
    const Json json = Json::parse(message);

    requireObject(json);
    requireVersion(json);

    if (
        !json.contains("type") ||
        !json["type"].is_string() ||
        json["type"].get<std::string>() != KCancelType
    ) {
        throw std::runtime_error(
            "unsupported RAG control "
            "request type"
        );
    }

    RagCancelRequest request;

    request.request_id = requireString(json, "request_id", false);

    if (!containsNonWhitespace(request.request_id)) {
        throw std::runtime_error(
            "missing RAG cancel request_id"
        );
    }

    return request;
}

std::string RagControlProtocol::encodeResponse(const RagCancelResponse& response) {
    validateResponse(response);

    const Json json = {
        {"version", KVersion},
        {"type", KCancelResultType},
        {"ok", response.ok},
        {"request_id", response.request_id},
        {"cancelled", response.cancelled},
        {"active_request_id", response.active_request_id},
        {"error", response.error}
    };

    return json.dump();
}

RagCancelResponse RagControlProtocol::decodeResponse(const std::string& message) {
    const Json json = Json::parse(message);

    requireObject(json);
    requireVersion(json);

    if (
        !json.contains("type") ||
        !json["type"].is_string() ||
        json["type"].get<std::string>() != KCancelResultType
    ) {
        throw std::runtime_error(
            "unsupported RAG control "
            "response type"
        );
    }

    RagCancelResponse response;
    response.ok = requireBoolean(json, "ok");
    response.request_id = requireString(json, "request_id", true);
    response.cancelled = requireBoolean(json, "cancelled");
    response.active_request_id = requireString(json, "active_request_id", true);
    response.error = requireString(json, "error", true);

    validateResponse(response);

    return response;
}
