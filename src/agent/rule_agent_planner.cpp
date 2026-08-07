#include "agent/rule_agent_planner.h"

namespace edge::agent {

std::string RuleAgentPlanner::name() const {
    return "rule_agent_planner";
}

AgentAction RuleAgentPlanner::plan(const std::string& user_input) const {
    const std::string msg = "user input must not be empty";
    if (user_input.empty()) {
        return AgentAction::failure(
            "user input must not be empty"
        );
    }

    if (
        contains(user_input, "温度")
        || contains(user_input, "湿度")
        || contains(user_input, "车里热不热")
        || contains(user_input, "车内热不热")
    ) {
        return AgentAction::toolCall(
            "environment-1",
            "get_cabin_environment",
            nlohmann::json::object()
        );
    }

    if (
        contains(user_input, "空调状态")
        || contains(user_input, "空调开了吗")
        || contains(user_input, "空调是否开启")
    ) {
        return AgentAction::toolCall(
            "ac-state-1",
            "get_air_conditioner_state",
            nlohmann::json::object()
        );
    }

    if (
        contains(user_input, "打开空调")
        || contains(user_input, "开启空调")
        || contains(user_input, "把空调打开")
    ) {
        return AgentAction::toolCall(
            "ac-control-1",
            "set_air_conditioner",
            {{"enabled", true}}
        );
    }

    if (
        contains(user_input, "关闭空调")
        || contains(user_input, "关掉空调")
        || contains(user_input, "把空调关掉")
    ) {
        return AgentAction::toolCall(
            "ac-control-1",
            "set_air_conditioner",
            {{"enabled", false}}
        );
    }

    return AgentAction::finalAnswer(
        "当前设备 Agent 只能查询车内温湿度，"
        "以及查询、打开或关闭模拟空调。"
    );
}

bool RuleAgentPlanner::contains(
    const std::string& text,
    const std::string& keyword
) {
    return text.find(keyword) != std::string::npos;
}

}