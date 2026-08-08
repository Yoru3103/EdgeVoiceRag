#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

#include "agent/agent_planner.h"
#include "agent/tool_registry.h"

namespace edge::agent {

struct AgentExecutorConfig {
    std::size_t maximum_steps = 4;
};
class AgentExecutor {
public:
    AgentExecutor(
        AgentPlanner& planner,
        ToolRegistry& registry,
        AgentExecutorConfig config = {}
    );

    AgentResponse run(
        const std::string& session_id,
        const std::string& user_input
    );

    bool hasPendingAction(const std::string& session_id) const;

    void clearSession(const std::string& session_id);

    bool cancel(const std::string& session_id);

private:
    struct PendingAction {
        AgentPlanningContext context;
        AgentToolCall tool_call;
    };

    AgentPlanner& planner_;
    ToolRegistry& registry_;
    AgentExecutorConfig config_;

    mutable std::mutex pending_mutex_;

    std::unordered_map<std::string, PendingAction> pending_actions_;

    AgentResponse continueExecution(
        const std::string& session_id,
        AgentPlanningContext context
    );

    AgentResponse handlePendingAction(
        const std::string& session_id,
        const std::string& user_input
    );

    static nlohmann::json buildTrace(const AgentPlanningContext& context);

    static bool isConfirmation(const std::string& text);

    static bool isCancellation(const std::string& text);

    static std::string buildConfirmationPrompt(const AgentToolCall& call);
};

}   // namespace edge::agent
