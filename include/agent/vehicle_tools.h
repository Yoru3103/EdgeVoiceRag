#pragma once

#include "agent/agent_tool.h"
#include "agent/vehicle_device.h"

namespace edge::agent {

class GetCabinEnvironmentTool final : public AgentTool {
public:
    explicit GetCabinEnvironmentTool(VehicleDevice& device);

    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;

    AgentToolResult execute(
        const nlohmann::json& arguments
    ) override;

private:
    VehicleDevice& device_;
};

// 为什么获取和设置要分成两个？
class GetAirConditionerStateTool final : public AgentTool {
public:
    explicit GetAirConditionerStateTool(
        VehicleDevice& device
    );

    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;

    AgentToolResult execute(
        const nlohmann::json& arguments
    ) override;

private:
    VehicleDevice& device_;
};

class SetAirConditionerTool final : public AgentTool {
public:
    explicit SetAirConditionerTool(VehicleDevice& device);

    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;

    bool requiresConfirmation() const override;

    AgentToolResult execute(
        const nlohmann::json& arguments
    ) override;

private:
    VehicleDevice& device_;
};

}   // namespace edge::agent
