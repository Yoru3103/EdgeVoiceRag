#pragma once

#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>

#include "agent/agent_planner.h"
#include "agent/tool_registry.h"
#include "llm_backend.h"

namespace edge::agent {

struct LlmAgentPlannerConfig {
    std::size_t maximum_response_bytes = 8192;

    std::string system_prompt =
        "你是一个车载设备Agent的任务规划器。"
        "你的职责只是决定调用哪个工具，或者直接回答用户。"
        "你不能声称已经执行尚未调用的工具。"
        "你不能生成Shell命令。"
        "你必须只输出一个JSON对象，不能输出Markdown、解释或其他文字。";
};

class LlmAgentPlanner final : public AgentPlanner {
public:
    LlmAgentPlanner(
        LlmBackend& llm_backend,
        const ToolRegistry& registry,
        LlmAgentPlannerConfig config = {}
    );

    std::string name() const override;

    AgentAction plan(const AgentPlanningContext& context) override;

    // 暴露给测试，验证模型输出解析逻辑。
    AgentAction parseResponse(const std::string& response) const;

    std::string buildPrompt(const AgentPlanningContext& context) const;

    // 语音打断时，agent可以取消正在运行的RKLLM推理
    bool cancel() override;

private:
    LlmBackend& llm_backend_;
    const ToolRegistry& registry_;
    LlmAgentPlannerConfig config_;

    static std::string extractJsonObject(const std::string& response);

    AgentAction parseToolCall(const nlohmann::json& root) const;

    static AgentAction parseFinalAnswer(const nlohmann::json& root);
};

}   // namespace edge::agent
