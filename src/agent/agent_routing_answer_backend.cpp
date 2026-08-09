#include "agent/agent_routing_answer_backend.h"

#include <exception>
#include <stdexcept>

#include "scope_exit.h"

namespace edge::agent {

AgentRoutingAnswerBackend::AgentRoutingAnswerBackend(
    AgentAnswerBackend& agent_backend,
    StreamingAnswerBackend& fallback_backend,
    CancellableAnswerBackend& fallback_cancellable
)
    : agent_backend_(agent_backend)
    , fallback_backend_(fallback_backend)
    , fallback_cancellable_(fallback_cancellable) {}

std::string AgentRoutingAnswerBackend::name() const {
    return "cpp_agent_router";
}

RagStreamQueryResult AgentRoutingAnswerBackend::query(
    const RagStreamRequest& request,
    const RagStreamEventHandler& handler
) const {
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

    // 安全分类必须最先执行
    const QueryClassification safety = safety_classifier_.classify(request.query);

    const bool emergency = safety.category == QueryCategory::Emergency;
    const bool pending = agent_backend_.hasPendingAction();
    const AgentIntentClassification intent = intent_classifier_.classify(request.query);

    const bool use_agent = !emergency && (pending || intent.matched());

    const ActiveBackend selected_backend = use_agent ? ActiveBackend::Agent : ActiveBackend::Fallback;

    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (active_) {
            return RagStreamQueryResult::failure(
                request.request_id,
                "another routed request is already active"
            );
        }
    }

    /*
     * 如果正在等待确认时出现紧急问题，
     * 清理之前的控制操作，防止之后误说“确认”触发旧任务。
     */
    if (emergency && pending) {
        agent_backend_.cancel(request.request_id);
    }

    setActive(
        request.request_id,
        selected_backend
    );

    auto active_guard = makeScopeExit(
        [this, request_id = request.request_id]() {
            clearActive(request_id);
        }
    );

    try {
        if (use_agent) {
            RagStreamQueryResult result = agent_backend_.query(
                request,
                handler
            );

            if (pending) {
                result.query_category = "agent_continuation";

                result.response_reason = "continue pending device workflow";
            } else {
                result.query_category = AgentIntentClassifier::intentToString(intent.intent);

                result.classification_confidence = intent.confidence;

                if (result.response_reason.empty()) {
                    result.response_reason = intent.reason;
                }
            }

            return result;
        }

        return fallback_backend_.query(request, handler);
    } catch (const std::exception& error) {
        return RagStreamQueryResult::failure(
            request.request_id,
            "agent routing failed: "
                + std::string(error.what())
        );
    }
}

CancellationResult AgentRoutingAnswerBackend::cancel(const std::string& request_id) const {
    if (request_id.empty()) {
        return CancellationResult::failure(
            request_id,
            "request_id must not be empty"
        );
    }

    ActiveBackend backend = ActiveBackend::None;
    std::string current_request_id;

    bool no_active_request = false;

    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        no_active_request = !active_;

        if (active_ && active_request_id_ != request_id) {
            return CancellationResult::success(
                request_id,
                false,
                active_request_id_
            );
        }

        backend = active_backend_;
        current_request_id = active_request_id_;
    }

    if (no_active_request) {
        return agent_backend_.cancel(request_id);
    }

    if (backend == ActiveBackend::Agent) {
        return agent_backend_.cancel(request_id);
    }

    if (backend == ActiveBackend::Fallback) {
        return fallback_cancellable_.cancel(
            request_id
        );
    }

    return CancellationResult::success(
        request_id,
        false,
        current_request_id
    );
}

void AgentRoutingAnswerBackend::setActive(
    const std::string& request_id,
    ActiveBackend backend
) const {
    std::lock_guard<std::mutex> lock(
        state_mutex_
    );

    active_ = true;
    active_request_id_ = request_id;
    active_backend_ = backend;
}

void AgentRoutingAnswerBackend::clearActive(
    const std::string& request_id
) const {
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (
        active_
        && active_request_id_ == request_id
    ) {
        active_ = false;
        active_request_id_.clear();
        active_backend_ = ActiveBackend::None;
    }
}

}   // namespace edge::agent
