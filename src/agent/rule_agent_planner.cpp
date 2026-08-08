#include "agent/rule_agent_planner.h"

#include <iomanip>
#include <sstream>

namespace edge::agent {

std::string RuleAgentPlanner::name() const {
    return "rule_agent_planner";
}

AgentAction RuleAgentPlanner::plan(const AgentPlanningContext& context) {
    if (context.user_input.empty()) {
        return AgentAction::failure(
            "user input must not be empty"
        );
    }

    if (!context.observations.empty()) {
        return handleObservation(
            context.observations.back()
        );
    }

    return planInitialAction(context.user_input);
}

AgentAction RuleAgentPlanner::planInitialAction(const std::string& user_input) {
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
        "当前设备Agent只能查询车内温湿度，"
        "以及查询、打开或关闭模拟空调。"
    );
}

AgentAction RuleAgentPlanner::handleObservation(const AgentObservation& observation) {
    const AgentToolResult& result = observation.tool_result;

    if (!result.ok) {
        return AgentAction::failure(
            "设备工具执行失败：" + result.error
        );
    }

    const std::string& tool_name = observation.tool_call.name;

    if (tool_name == "get_cabin_environment") {
        const float temperature =
            result.data.at("temperature_c").get<float>();

        const float humidity =
            result.data.at("humidity_percent").get<float>();

        return AgentAction::finalAnswer(
            "当前车内温度为"
            + formatNumber(temperature)
            + "摄氏度，湿度为"
            + formatNumber(humidity)
            + "%。"
        );
    }

    if (tool_name == "get_air_conditioner_state") {
        const bool enabled =
            result.data.at("enabled").get<bool>();

        return AgentAction::finalAnswer(
            enabled
                ? "模拟空调当前处于开启状态，指示灯已点亮。"
                : "模拟空调当前处于关闭状态，指示灯已熄灭。"
        );
    }

    if (tool_name == "set_air_conditioner") {
        const bool enabled =
            result.data.at("enabled").get<bool>();

        return AgentAction::finalAnswer(
            enabled
                ? "模拟空调已开启，指示灯已点亮。"
                : "模拟空调已关闭，指示灯已熄灭。"
        );
    }

    return AgentAction::finalAnswer(
        "工具执行成功。"
    );
}

bool RuleAgentPlanner::contains(
    const std::string& text,
    const std::string& keyword
) {
    return text.find(keyword) != std::string::npos;
}

std::string RuleAgentPlanner::formatNumber(float value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    return stream.str();
}

}   // namespace edge::agent
