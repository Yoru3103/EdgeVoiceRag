#include "agent/vehicle_tools.h"

#include <cmath>

namespace edge::agent {

namespace {

    bool hasNoArguments(const nlohmann::json& arguments) {
        return arguments.is_object() && arguments.empty();
    }

}   // namespace

GetCabinEnvironmentTool::GetCabinEnvironmentTool(VehicleDevice& device)
    : device_(device) {}

std::string GetCabinEnvironmentTool::name() const {
    return "get_cabin_environment";
}

std::string GetCabinEnvironmentTool::description() const {
    return "读取当前车内温度和湿度";
}

nlohmann::json GetCabinEnvironmentTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", nlohmann::json::object()},
        {"additionalProperties", false}
    };
}

AgentToolResult GetCabinEnvironmentTool::execute(const nlohmann::json& arguments) {
    if (!hasNoArguments(arguments)) {
        return AgentToolResult::failure(
            "get_cabin_environment does not accept arguments"
        );
    }

    const CabinEnvironment environment = device_.readEnvironment();

    if (
        !std::isfinite(environment.temperature_c) ||
        !std::isfinite(environment.humidity_percent)
    ) {
        return AgentToolResult::failure(
            "environment sensor returned invalid data"
        );
    }

    return AgentToolResult::success({
        {"temperature_c", environment.temperature_c},
        {"humidity_percent", environment.humidity_percent}
    });
}

GetAirConditionerStateTool::GetAirConditionerStateTool(VehicleDevice& device)
    :device_(device) {}

std::string GetAirConditionerStateTool::name() const {
    return "get_air_conditioner_state";
}

std::string GetAirConditionerStateTool::description() const {
    return "查询模拟空调和指示灯的当前状态";
}

nlohmann::json GetAirConditionerStateTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", nlohmann::json::object()},
        {"additionalProperties", false}
    };
}

AgentToolResult GetAirConditionerStateTool::execute(const nlohmann::json& arguments) {
    if (!hasNoArguments(arguments)) {
        return AgentToolResult::failure(
            "get_air_conditioner_state does not accept arguments"
        );
    }

    const bool enabled = device_.airConditionerEnabled();

    return AgentToolResult::success({
        {"enabled", enabled},
        {"indicator_led_on", enabled}
    });
}

SetAirConditionerTool::SetAirConditionerTool(VehicleDevice& device) 
    : device_(device) {}

std::string SetAirConditionerTool::name() const {
    return "set_air_conditioner";
}

std::string SetAirConditionerTool::description() const {
    return "打开或关闭模拟空调，并同步改变指示灯状态";
}

nlohmann::json SetAirConditionerTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"enabled", {
                {"type", "boolean"},
                {"description", "true 表示开启，false 表示关闭"}
            }}
        }},
        {"required", {"enabled"}},
        {"additionalProperties", false}
    };
}

bool SetAirConditionerTool::requiresConfirmation() const {
    return true;
}

AgentToolResult SetAirConditionerTool::execute(const nlohmann::json& arguments) {
    if (!arguments.is_object()) {
        return AgentToolResult::failure(
            "tool arguments must be an object"
        );
    }

    if (
        !arguments.contains("enabled") ||
        !arguments.at("enabled").is_boolean()
    ) {
        return AgentToolResult::failure(
            "enabled must be a boolean"
        );
    }

    if (arguments.size() != 1) {
        return AgentToolResult::failure(
            "set_air_conditioner received unknown arguments"
        );
    }

    const bool enabled = arguments.at("enabled").get<bool>();

    if (!device_.setAirConditionerEnabled(enabled)) {
        return AgentToolResult::failure(
            "failed to update air conditioner state"
        );
    }

    // 写后读取，验证设备状态。
    const bool actual_state = device_.airConditionerEnabled();

    if (actual_state != enabled) {
        return AgentToolResult::failure(
            "air conditioner state verification failed"
        );
    }

    return AgentToolResult::success({
        {"enabled", actual_state},
        {"indicator_led_on", actual_state}
    });
}

}   // namespace edge::agent
