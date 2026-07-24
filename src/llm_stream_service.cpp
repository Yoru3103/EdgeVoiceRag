#include "llm_stream_service.h"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "llm_protocol.h"
#include "scope_exit.h"

namespace {

double elapsedMilliseconds(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start
    ).count();
}

}   // namespace

LlmStreamService::LlmStreamService(LlmBackend& backend)
    : backend_(backend) {
}

void LlmStreamService::handleMessage(
    const std::string& message,
    const LlmEventEmitter& emitter
) {
    if (!emitter) {
        throw std::invalid_argument(
            "LLM event emitter must not be empty"
        );
    }

    const auto start = std::chrono::steady_clock::now();

    std::string request_id = tryExtractRequestId(message);

    std::size_t sequence = 0;

    try {
        const LlmRequest request = LlmProtocol::decodeRequest(message);

        request_id = request.request_id;

        if (!request.stream) {
            throw std::runtime_error(
                "stream request must set stream=true"
            );
        }

        setActiveRequest(request_id);

        auto active_guard = makeScopeExit(
            [this, request_id]() {
                clearActiveRequest(request_id);
            }
        );

        const LlmGenerationResult generation = backend_.generateStream(
            request.prompt,
            [&](const std::string& chunk) {
                LlmStreamEvent event;

                event.type = LlmStreamEventType::Chunk;
                event.ok = true;
                event.request_id = request_id;
                event.delta = chunk;
                event.backend = backend_.name();
                event.sequence = sequence++;
                event.elapsed_ms = elapsedMilliseconds(start);
                event.finished = false;

                emitter(LlmProtocol::encodeStreamEvent(event));
            }
        );

        if (!generation.ok) {
            LlmStreamEvent event;

            event.type = LlmStreamEventType::Error;
            event.ok = false;
            event.request_id = request_id;
            event.backend = backend_.name();
            event.error = generation.error;
            event.sequence = sequence;
            event.elapsed_ms = elapsedMilliseconds(start);
            event.finished = true;

            emitter(LlmProtocol::encodeStreamEvent(event));

            return;
        }

        LlmStreamEvent event;

        event.type = LlmStreamEventType::Finished;
        event.ok = true;
        event.request_id = request_id;
        event.answer = generation.answer;
        event.backend = backend_.name();
        event.sequence = sequence;
        event.elapsed_ms = elapsedMilliseconds(start);
        event.finished = true;

        emitter(LlmProtocol::encodeStreamEvent(event));
    } catch (const std::exception& error) {
        if (request_id.empty()) {
            throw;
        }

        LlmStreamEvent event;

        event.type = LlmStreamEventType::Error;
        event.ok = false;
        event.request_id = request_id;
        event.backend = backend_.name();
        event.error = error.what();
        event.sequence = sequence;
        event.elapsed_ms = elapsedMilliseconds(start);
        event.finished = true;

        emitter(LlmProtocol::encodeStreamEvent(event));
    }
}

std::string LlmStreamService::activeRequestId() const {
    std::lock_guard<std::mutex> lock(active_mutex_);

    return active_request_id_;
}

bool LlmStreamService::cancel(const std::string& request_id) {
    if (request_id.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(active_mutex_);

    if (active_request_id_ != request_id) {
        return false;
    }

    return backend_.cancel();
}

void LlmStreamService::setActiveRequest(const std::string& request_id) {
    std::lock_guard<std::mutex> lock(active_mutex_);    // RAII

    if (!active_request_id_.empty()) {
        throw std::runtime_error(
            "another LLM stream request "
            "is already running"
        );
    }

    active_request_id_ = request_id;
}

void LlmStreamService::clearActiveRequest(const std::string& request_id) {
    std::lock_guard<std::mutex> lock(active_mutex_);

    if (active_request_id_ == request_id) {
        active_request_id_.clear();
    }
}

std::string LlmStreamService::tryExtractRequestId(const std::string& message) {
    try {
        const auto json = nlohmann::json::parse(message);

        return json.value("request_id", "");
    } catch (const std::exception&) {
        return "";
    }
}
