#pragma once

#include <mutex>
#include <string>

#include "agent/agent_answer_backend.h"
#include "agent/agent_intent_classifier.h"
#include "cancellable_answer_backend.h"
#include "query_classifier.h"
#include "streaming_answer_backend.h"

namespace edge::agent {

class AgentRoutingAnswerBackend final
    : public StreamingAnswerBackend
    , public CancellableAnswerBackend {
public:
    AgentRoutingAnswerBackend(
        AgentAnswerBackend& agent_backend,
        StreamingAnswerBackend& fallback_backend,
        CancellableAnswerBackend& fallback_cancellable
    );

    std::string name() const override;

    RagStreamQueryResult query(
        const RagStreamRequest& request,
        const RagStreamEventHandler& handler = {}
    ) const override;

    CancellationResult cancel(
        const std::string& request_id
    ) const override;

private:
    enum class ActiveBackend {
        None,
        Agent,
        Fallback
    };

    AgentAnswerBackend& agent_backend_;
    StreamingAnswerBackend& fallback_backend_;
    CancellableAnswerBackend& fallback_cancellable_;

    AgentIntentClassifier intent_classifier_;
    QueryClassifier safety_classifier_;

    mutable std::mutex state_mutex_;
    mutable bool active_ = false;
    mutable std::string active_request_id_;
    mutable ActiveBackend active_backend_ =
        ActiveBackend::None;

    void setActive(
        const std::string& request_id,
        ActiveBackend backend
    ) const;

    void clearActive(
        const std::string& request_id
    ) const;
};

}   // namespace edge::agent
