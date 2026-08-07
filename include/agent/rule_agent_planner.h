#pragma once

#include <string>

#include "agent/agent_planner.h"

namespace edge::agent {

class RuleAgentPlanner final : public AgentPlanner {
public:
    std::string name() const override;

    AgentAction plan(const std::string& user_input) const override;

private:
    static bool contains(
        const std::string& text,
        const std::string& keyword
    );
};

}   // namespace edge::agent