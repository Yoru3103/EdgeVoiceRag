#include "agent/agent_answer_backend.h"

#include <chrono>
#include <stdexcept>
#include <utility>

#include "scope_exit.h"

namespace edge::agent {

namespace {

using Clock = std::chrono::steady_clock;

double elapsedMilliseconds(const Clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(
        Clock::now() - start
    ).count();
}

void emitChunk(
    const RagStreamRequest& request,
    const std::string& answer,
    const std::string& backend,
    double elapsed_ms,
    const RagStreamEventHandler& handler
) {
    if (!handler) {
        return;
    }

    RagStreamEvent event;
    event.type = RagStreamEventType::Chunk;
    event.ok = true;
    event.request_id = request.request_id;
    event.sequence = 0;
    event.delta = answer;
    event.backend = backend;
    event.elapsed_ms = elapsed_ms;

    handler(event);
}

void emitFinished(
    const RagStreamRequest& request,
    const std::string& answer,
    const std::string& backend,
    double elapsed_ms,
    const RagStreamEventHandler& handler
) {
    if (!handler) {
        return;
    }

    RagStreamEvent event;
    event.type = RagStreamEventType::Finished;
    event.ok = true;
    event.request_id = request.request_id;
    event.sequence = 1;
    event.answer = answer;
    event.backend = backend;
    event.elapsed_ms = elapsed_ms;
    event.finished = true;

    handler(event);
}

void emitError(
    const RagStreamRequest& request,
    const std::string& error,
    const std::string& backend,
    double elapsed_ms,
    const RagStreamEventHandler& handler
) {
    if (!handler) {
        return;
    }

    RagStreamEvent event;
    event.type = RagStreamEventType::Error;
    event.ok = false;
    event.request_id = request.request_id;
    event.sequence = 0;
    event.error = error;
    event.backend = backend;
    event.elapsed_ms = elapsed_ms;
    event.finished = true;

    handler(event);
}

}   // namespace

AgentAnswerBackend::AgentAnswerBackend(
    AgentExecutor& executor,
    AgentAnswerBackendConfig config
)
    : executor_(executor)
    , config_(std::move(config)) {
    if (config_.session_id.empty()) {
        throw std::invalid_argument(
            "agent session_id must not be empty"
        );  
    }
}

std::string AgentAnswerBackend::name() const {
    return "cpp_vehicle_agent";
}

RagStreamQueryResult AgentAnswerBackend::query(
    const RagStreamRequest& request,
    const RagStreamEventHandler& handler
) const {
    const auto start = Clock::now();

    if (request.request_id.empty()) {
        return RagStreamQueryResult::failure(
            request.request_id,
            "request_id must not be empty"
        );
    }

    if (request.query.empty()) {
        return RagStreamQueryResult::failure(
            request.request_id,
            "query must not be empty"
        );
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (active_) {
            return RagStreamQueryResult::failure(
                request.request_id,
                "another agent request is already active"
            );
        }

        active_ = true;
        active_request_id_ = request.request_id;
    }

    auto active_guard = makeScopeExit(
        [this, request_id = request.request_id]() {
            clearActiveRequest(request_id);
        }
    );

    try {
        const AgentResponse response = 
            executor_.run(
                config_.session_id,
                request.query
            );

        const double elapsed_ms = elapsedMilliseconds(start);

        if (!response.ok) {
            const std::string error = response.error.empty() ? "agent execution failed" : response.error;

            emitError(
                request,
                error,
                name(),
                elapsed_ms,
                handler
            );

            RagStreamQueryResult result = RagStreamQueryResult::failure(
                request.request_id,
                error,
                elapsed_ms
            );

            result.backend = name();
            result.response_mode = "agent_error";
            result.query_category = "device_agent";

            return result;
        }

        if (response.answer.empty()) {
            return RagStreamQueryResult::failure(
                request.request_id,
                "agent returned an empty answer",
                elapsed_ms
            );
        }

        const std::string response_mode =
            response.state == AgentResponseState::WaitingForConfirmation
                ? "agent_confirmation"
                : "agent_tool";

        emitChunk(
            request,
            response.answer,
            name() + "/" + response_mode,
            elapsed_ms,
            handler
        );

        emitFinished(
            request,
            response.answer,
            name() + "/" + response_mode,
            elapsed_ms,
            handler
        );

        RagStreamQueryResult result = 
            RagStreamQueryResult::success(
                request.request_id,
                response.answer,
                name() + "/" + response_mode,
                "",
                elapsed_ms
            );

        result.response_mode = response_mode;
        result.response_reason = 
            response.state == AgentResponseState::WaitingForConfirmation
                ? "device write operation requires confirmation"
                : "device agent completed the workflow";
        
        result.query_category = "device_agent";
        result.classification_confidence = 1.0F;
        result.retrieval_result_count = 0;

        return result;
    } catch (const std::exception& error) {
        const std::string message = "agent backend failed: " + std::string(error.what());

        emitError(
            request,
            message,
            name(),
            elapsedMilliseconds(start),
            handler
        );

        return RagStreamQueryResult::failure(
            request.request_id,
            message,
            elapsedMilliseconds(start)
        );
    }
}

CancellationResult AgentAnswerBackend::cancel(const std::string& request_id) const {
    if (request_id.empty()) {
        return CancellationResult::failure(
            request_id,
            "request_id must not be empty"
        );
    }

    std::string current_request_id;

    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        current_request_id = active_request_id_;

        if (active_ && active_request_id_ != request_id) {
            return CancellationResult::success(
                request_id,
                false,
                active_request_id_
            );
        }
    }

    const bool cancelled = executor_.cancel(config_.session_id);

    return CancellationResult::success(
        request_id,
        cancelled,
        current_request_id
    );
}

bool AgentAnswerBackend::hasPendingAction() const {
    return executor_.hasPendingAction(config_.session_id);
}

void AgentAnswerBackend::clearActiveRequest(const std::string& request_id) const {
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (active_ && active_request_id_ == request_id) {
        active_ = false;
        active_request_id_.clear();
    }
}

}   // namespace edge::agent
