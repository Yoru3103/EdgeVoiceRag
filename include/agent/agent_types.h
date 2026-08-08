#pragma once

#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace edge::agent {

enum class AgentActionType {
    FinalAnswer,
    ToolCall,
    Error
};

// 一次结构化工具调用
struct AgentToolCall {
    std::string id;
    std::string name;
    nlohmann::json arguments = nlohmann::json::object();
};

// Planncer做出的决策
struct AgentAction {
    AgentActionType type = AgentActionType::Error;
    AgentToolCall tool_call;
    std::string answer;
    std::string error;

    static AgentAction finalAnswer(std::string text) {
        AgentAction action;
        action.type = AgentActionType::FinalAnswer;
        action.answer = std::move(text);
        return action;
    }

    static AgentAction toolCall(
        std::string id,
        std::string name,
        nlohmann::json arguments
    ) {
        AgentAction action;
        action.type = AgentActionType::ToolCall;
        action.tool_call.id = std::move(id);
        action.tool_call.name = std::move(name);
        action.tool_call.arguments = std::move(arguments);
        return action;
    }

    static AgentAction failure(const std::string& message) {
        AgentAction action;
        action.type = AgentActionType::Error;
        action.error = std::move(message);
        return action;
    }
};

// 硬件执行结果
struct AgentToolResult {
    bool ok = false;
    nlohmann::json data = nlohmann::json::object();
    std::string error;

    static AgentToolResult success(nlohmann::json value) {
        AgentToolResult result;
        result.ok = true;
        result.data = std::move(value);
        return result;
    }

    static AgentToolResult failure(std::string message) {
        AgentToolResult result;
        result.ok = false;
        result.error = std::move(message);
        return result;
    }
};

struct AgentObservation {
    AgentToolCall tool_call;
    AgentToolResult tool_result;
};

// 保存原始任务以及每一步工具执行结果
struct AgentPlanningContext {
    std::string user_input;
    std::vector<AgentObservation> observations;
};

enum class AgentResponseState {
    Completed,
    WaitingForConfirmation,
    Failed
};

// 最终交给语音助手的内容
struct AgentResponse {
    bool ok = false;
    AgentResponseState state = AgentResponseState::Failed;

    std::string answer;
    std::string error;

    // 便于调试、记录
    std::string executed_tool;
    nlohmann::json observation = nlohmann::json::object();

    nlohmann::json trace = nlohmann::json::array();

    static AgentResponse completed(
        std::string text,
        std::string tool = {},
        nlohmann::json observation = nlohmann::json::object(),
        nlohmann::json execution_trace = nlohmann::json::array()
    ) {
        AgentResponse response;
        response.ok = true;
        response.state = AgentResponseState::Completed;
        response.answer = std::move(text);
        response.executed_tool = std::move(tool);
        response.observation = std::move(observation);
        response.trace = std::move(execution_trace);
        return response;
    }

    static AgentResponse waiting(std::string text) {
        AgentResponse response;
        response.ok = true;
        response.state = AgentResponseState::WaitingForConfirmation;
        response.answer = std::move(text);
        return response;
    }

    static AgentResponse failure(std::string message) {
        AgentResponse response;
        response.error = std::move(message);
        return response;
    }
};

}   // namespace edge::agent




