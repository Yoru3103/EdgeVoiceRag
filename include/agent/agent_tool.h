#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "agent/agent_types.h"

namespace edge::agent {

class AgentTool {
public:
    virtual ~AgentTool() = default;

    virtual std::string name() const = 0;

    virtual std::string description() const = 0;

    // 将来提供给LLM，让模型知道参数格式
    virtual nlohmann::json parametersSchema() const = 0;

    // 会改变外部设备状态的工具返回true
    virtual bool requiresConfirmation() const {
        return false;
    }

    virtual AgentToolResult execute(
        const nlohmann::json& arguments
    ) = 0;
};

}   // namespace edge::agent
