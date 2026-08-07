#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "agent/agent_tool.h"

namespace edge::agent {

class ToolRegistry {
public:
    void registerTool(std::unique_ptr<AgentTool> tool);

    AgentTool* find(const std::string& name);
    const AgentTool* find(const std::string& name) const;

    AgentToolResult execute(const AgentToolCall& call);

    // 将来可以直接把这些定义交给 LLM Planner。
    nlohmann::json definition() const;

private:
    std::unordered_map<std::string, std::unique_ptr<AgentTool>> tools_;
};

}   // namespace edge::agent
