#pragma once

#include <mutex>
#include <string>

#include "agent/agent_executor.h"
#include "cancellable_answer_backend.h"
#include "streaming_answer_backend.h"

namespace edge::agent {

struct AgentAnswerBackendConfig {
    // 当前板载程序只有一个连续语音会话。
    // 不能使用request_id，因为每一轮语音的request_id会变化，
    // 会导致下一轮“确认”找不到上一轮Pending Action。
    std::string session_id = "board-voice-session";
};

class AgentAnswerBackend final
    : public StreamingAnswerBackend
    , public CancellableAnswerBackend {
public:
    AgentAnswerBackend(
        AgentExecutor& executor,
        AgentAnswerBackendConfig config = {}
    );

    std::string name() const override;

    RagStreamQueryResult query(
        const RagStreamRequest& request,
        const RagStreamEventHandler& handler = {}
    ) const override;

    CancellationResult cancel(
        const std::string& request_id
    ) const override;

    bool hasPendingAction() const;

private:
    AgentExecutor& executor_;
    AgentAnswerBackendConfig config_;

    mutable std::mutex state_mutex_;
    mutable bool active_ = false;
    mutable std::string active_request_id_;

    void clearActiveRequest(const std::string& request_id) const;
};

}   // namespace edge::agent
