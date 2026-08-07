#pragma once

#include <string>

#include "agent/agent_types.h"

namespace edge::agent {

// 只负责决定下一步做什么，不负责执行工具
class AgentPlanner {
public:
    virtual ~AgentPlanner() = default;

    virtual std::string name() const = 0;

    virtual AgentAction plan(const std::string& user_input) const = 0;
};

}   // namespace edge::agent
