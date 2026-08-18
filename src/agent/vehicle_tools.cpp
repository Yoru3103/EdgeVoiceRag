#include "agent/vehicle_tools.h"

#include <cmath>
#include <exception>

namespace edge::agent {

namespace {

bool hasNoArguments(const nlohmann::json& arguments) {
    return arguments.is_object() && arguments.empty();
}

constexpr double kMinimumTemperatureThresholdC = -50.0;
constexpr double kMaximumTemperatureThresholdC = 100.0;

bool isSupportedTemperatureOperator(const std::string& comparison_operator) {
    return
        comparison_operator == "gt"
        || comparison_operator == "ge"
        || comparison_operator == "lt"
        || comparison_operator == "le";
}

bool compareTemperature(
    double temperature_c,
    double threshold_c,
    const std::string& comparison_operator
) {
    if (comparison_operator == "gt") {
        return temperature_c > threshold_c;
    }

    if (comparison_operator == "ge") {
        return temperature_c >= threshold_c;
    }

    if (comparison_operator == "lt") {
        return temperature_c < threshold_c;
    }

    return temperature_c <= threshold_c;
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

    CabinEnvironment environment;

    try {
        environment = device_.readEnvironment();
    } catch (const std::exception& error) {
        return AgentToolResult::failure(
            std::string("environment sensor read failed: ")
            + error.what()
        );
    }

    if (!std::isfinite(environment.temperature_c) || !std::isfinite(environment.humidity_percent)) {
        return AgentToolResult::failure(
            "environment sensor returned invalid data"
        );
    }

    return AgentToolResult::success({
        {"temperature_c", environment.temperature_c},
        {"humidity_percent", environment.humidity_percent}
    });
}

CheckCabinTemperatureConditionTool::CheckCabinTemperatureConditionTool(VehicleDevice& device)
    : device_(device) {}

std::string CheckCabinTemperatureConditionTool::name() const {
    return "check_cabin_temperature_condition";
}

std::string CheckCabinTemperatureConditionTool::description() const {
    return
        "读取当前车内温度，并由C++确定性判断温度条件。"
        "仅用于用户提出“温度满足某个条件时执行操作”的任务。"
        "operator只能是gt、ge、lt、le，分别表示大于、"
        "大于等于、小于、小于等于。"
        "普通温湿度查询应使用get_cabin_environment。";
}

nlohmann::json CheckCabinTemperatureConditionTool::parametersSchema() const {
    return {
        {"type", "object"},
        {
            "properties",
            {
                {
                    "operator",
                    {
                        {"type", "string"},
                        {
                            "enum",
                            {"gt", "ge", "lt", "le"}
                        },
                        {
                            "description",
                            "gt大于，ge大于等于，"
                            "lt小于，le小于等于"
                        }
                    }
                },
                {
                    "threshold_c",
                    {
                        {"type", "number"},
                        {
                            "minimum",
                            kMinimumTemperatureThresholdC
                        },
                        {
                            "maximum",
                            kMaximumTemperatureThresholdC
                        },
                        {
                            "description",
                            "摄氏温度阈值"
                        }
                    }
                }
            }
        },
        {
            "required",
            {"operator", "threshold_c"}
        },
        {"additionalProperties", false}
    };
}

AgentToolResult CheckCabinTemperatureConditionTool::execute(const nlohmann::json& arguments) {
    if (!arguments.is_object()) {
        return AgentToolResult::failure(
            "tool arguments must be an object"
        );
    }

    if (
        !arguments.contains("operator")
        || !arguments.contains("threshold_c")
    ) {
        return AgentToolResult::failure(
            "operator and threshold_c are required"
        );
    }

    if (arguments.size() != 2) {
        return AgentToolResult::failure(
            "check_cabin_temperature_condition "
            "received unknown arguments"
        );
    }

    if (!arguments.at("operator").is_string()) {
        return AgentToolResult::failure(
            "operator must be a string"
        );
    }

    const std::string comparison_operator = arguments.at("operator").get<std::string>();

    if (
        !isSupportedTemperatureOperator(comparison_operator)
    ) {
        return AgentToolResult::failure(
            "operator must be one of: gt, ge, lt, le"
        );
    }

    if (!arguments.at("threshold_c").is_number()) {
        return AgentToolResult::failure(
            "threshold_c must be a number"
        );
    }

    const double threshold_c = arguments.at("threshold_c").get<double>();

    if (!std::isfinite(threshold_c)) {
        return AgentToolResult::failure(
            "threshold_c must be finite"
        );
    }

    if (
        threshold_c < kMinimumTemperatureThresholdC
        || threshold_c > kMaximumTemperatureThresholdC
    ) {
        return AgentToolResult::failure(
            "threshold_c is outside the allowed range"
        );
    }

    CabinEnvironment environment;

    try {
        environment = device_.readEnvironment();
    } catch (const std::exception& error) {
        return AgentToolResult::failure(
            std::string("environment sensor read failed: ")
            + error.what()
        );
    }

    const double temperature_c = static_cast<double>(environment.temperature_c);

    if (!std::isfinite(temperature_c)) {
        return AgentToolResult::failure(
            "environment sensor returned "
            "an invalid temperature"
        );
    }

    const bool matched = compareTemperature(
        temperature_c,
        threshold_c,
        comparison_operator
    );

    return AgentToolResult::success({
        {"temperature_c", temperature_c},
        {"operator", comparison_operator},
        {"threshold_c", threshold_c},
        {"matched", matched}
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
