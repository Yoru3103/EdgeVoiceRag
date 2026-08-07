#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include "agent/agent_planner.h"
#include "agent/tool_registry.h"

namespace edge::agent {

class AgentExecutor {
public:
    AgentExecutor(
        AgentPlanner& planner,
        ToolRegistry& registry
    );

    AgentResponse run(
        const std::string& session_id,
        const std::string& user_input
    );

    bool hasPendingAction(const std::string& session_id) const;

    void clearSession(const std::string& session_id);

private:
    struct PendingAction {
        AgentToolCall tool_call;
    };

    AgentPlanner& planner_;
    ToolRegistry& registry_;

    mutable std::mutex pending_mutex_;
    std::unordered_map<std::string, PendingAction> pending_actions_;

    AgentResponse handlePendingAction(
        const std::string& session_id,
        const std::string& user_input
    );

    AgentResponse processAction(
        const std::string& session_id,
        const AgentAction& action
    );

    AgentResponse executeTool(const AgentToolCall& call);

    static AgentResponse renderToolResult(
        const AgentToolCall& call,
        const AgentToolResult& result
    );

    static bool isConfirmation(const std::string& text);

    static bool isCancellation(const std::string& text);

    static std::string buildConfirmationPrompt(const AgentToolCall& call);
};

}   // namespace edge::agent
