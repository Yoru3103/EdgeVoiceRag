#pragma once

#include <string>

#include "agent/agent_planner.h"

namespace edge::agent {

class RuleAgentPlanner final : public AgentPlanner {
public:
    std::string name() const override;

    AgentAction plan(const AgentPlanningContext& context) override;

private:
    static AgentAction planInitialAction(const std::string& user_input);

    static AgentAction handleObservation(const AgentObservation& observation);

    static bool contains(
        const std::string& text,
        const std::string& keyword
    );

    static std::string formatNumber(float value);
};

}   // namespace edge::agent