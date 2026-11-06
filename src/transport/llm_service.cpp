#include "llm_service.h"

#include <chrono>
#include <exception>
#include <string>

#include <nlohmann/json.hpp>

#include "llm_protocol.h"

LlmService::LlmService(LlmBackend& backend)
    : backend_(backend) {
}

std::string LlmService::handleMessage(const std::string& message) {
    const auto start = std::chrono::steady_clock::now();

    std::string request_id = tryExtractRequestId(message);

    try {
        const LlmRequest request = LlmProtocol::decodeRequest(message);
        request_id = request.request_id;

        if (request.stream) {
            throw std::runtime_error(
                "streaming generation is not supported yet"
            );
        }

        const LlmGenerationResult generation = backend_.generate(request.prompt);

        const auto end = std::chrono::steady_clock::now();

        const double elapsed_time =
            std::chrono::duration<double, std::milli>(
                end - start
            ).count();

        LlmResponse response;

        response.ok = generation.ok;
        response.request_id = request_id;
        response.answer = generation.answer;
        response.backend = backend_.name();
        response.error = generation.error;
        response.elapsed_ms = elapsed_time;
        response.finished = true;

        return LlmProtocol::encodeResponse(response);
    } catch (const std::exception& error) {
        const auto end = std::chrono::steady_clock::now();

        const double elapsed_ms = std::chrono::duration<double, std::milli>(
            end - start
        ).count();

        LlmResponse response;

        response.ok = false;
        response.request_id = request_id;
        response.backend = backend_.name();
        response.error = error.what();
        response.elapsed_ms = elapsed_ms;
        response.finished = true;

        return LlmProtocol::encodeResponse(response);
    }
}

std::string LlmService::tryExtractRequestId(const std::string& message) {
    try {
        const auto json = nlohmann::json::parse(message);
        return json.value("request_id", "");
    } catch (const std::exception&) {
        return "";
    }
}