#include "agent/llm_agent_planner.h"

#include <stdexcept>
#include <utility>

#include "logger.h"

namespace edge::agent {

LlmAgentPlanner::LlmAgentPlanner(
    LlmBackend& llm_backend,
        const ToolRegistry& registry,
        LlmAgentPlannerConfig config
)
    : llm_backend_(llm_backend)
    , registry_(registry)
    , config_(std::move(config)) {
    if (config_.maximum_response_bytes == 0) {
        throw std::invalid_argument(
            "maximum_response_bytes must be greater than zero"
        );
    }

    if (config_.system_prompt.empty()) {
        throw std::invalid_argument(
            "agent system prompt must not be empty"
        );
    }
}

std::string LlmAgentPlanner::name() const {
    return "llm_agent_planner/" + llm_backend_.name();
}

AgentAction LlmAgentPlanner::plan(const AgentPlanningContext& context) {
    if (context.user_input.empty()) {
        return AgentAction::failure("user input must not be empty");
    }

    const std::string prompt = buildPrompt(context);

    Logger::log(
        LogLevel::Info,
        "AGENT_PLANNER_BEGIN "
        + nlohmann::json{
            {"user_input", context.user_input},
            {"observation_count", context.observations.size()}
        }.dump()
    );

    const LlmGenerationResult generation = llm_backend_.generate(prompt);

    Logger::log(
        generation.ok ? LogLevel::Info : LogLevel::Error,
        "AGENT_PLANNER_RESPONSE "
        + nlohmann::json{
            {"ok", generation.ok},
            {"answer", generation.answer},
            {"error", generation.error}
        }.dump()
    );

    if (!generation.ok) {
        return AgentAction::failure(
            "LLM planning failed: " + generation.error
        );
    }

    if (generation.answer.empty()) {
        return AgentAction::failure(
            "LLM planner returned an empty response"
        );
    }

    if (generation.answer.size() > config_.maximum_response_bytes) {
        return AgentAction::failure(
            "LLM planner response is too large"
        );
    }

    return parseResponse(generation.answer);
}

std::string LlmAgentPlanner::buildPrompt(const AgentPlanningContext& context) const {
    const nlohmann::json tools = registry_.definitions();

    nlohmann::json history = nlohmann::json::array();

    for (const AgentObservation& observation : context.observations) {
        history.push_back({
            {
                "tool_call",
                {
                    {"id", observation.tool_call.id},
                    {"name", observation.tool_call.name},
                    {
                        "arguments",
                        observation.tool_call.arguments
                    }
                }
            },
            {
                "observation",
                {
                    {"ok", observation.tool_result.ok},
                    {"data", observation.tool_result.data},
                    {"error", observation.tool_result.error}
                }
            }
        });
    }

    nlohmann::json tool_call_example = {
        {"type", "tool_call"},
        {
            "tool_call",
            {
                {"id", "call-1"},
                {"name", "get_cabin_environment"},
                {"arguments", nlohmann::json::object()}
            }
        }
    };

    nlohmann::json final_answer_example = {
        {"type", "final_answer"},
        {"answer", "任务已经完成。"
        }
    };

    std::string prompt;
    prompt.reserve(4096);

    prompt += config_.system_prompt;

    prompt += "\n\n可用工具：\n";
    prompt += tools.dump(2);

    prompt += "\n\n用户原始任务：\n";
    prompt += context.user_input;

    prompt += "\n\n已经执行的步骤和观察结果：\n";
    prompt += history.dump(2);

    prompt += "\n\n调用工具时输出：\n";
    prompt += tool_call_example.dump();

    prompt += "\n\n任务完成时输出：\n";
    prompt += final_answer_example.dump();

    prompt +=
        "\n\n规则："
        "\n1. 如果没有足够信息，选择一个工具。"
        "\n2. 如果Observation已经足够，输出final_answer。"
        "\n3. 可以根据Observation继续调用其他工具。"
        "\n4. 不得重复调用已经获得有效结果的只读工具。"
        "\n5. 工具执行失败时应根据错误生成简短回答，"
        "不要假装成功。"
        "\n6. 不得声称未执行的控制操作已经完成。"
        "\n7. 只能输出一个JSON对象。"
        "\n8. 不输出思考过程。";

    prompt += "\n\n下一步JSON输出：";

    return prompt;
}

AgentAction LlmAgentPlanner::parseResponse(const std::string& response) const {
    if (response.empty()) {
        return AgentAction::failure("cannot parse empty LLM response");
    }

    try {
        const std::string json_text = extractJsonObject(response);

        const nlohmann::json root = nlohmann::json::parse(json_text);

        if (!root.is_object()) {
            return AgentAction::failure(
                "LLM response root must be an object"
            );
        }

        if (!root.contains("type") || !root.at("type").is_string()) {
            return AgentAction::failure(
                "LLM response must contain string field: type"
            );
        }

        const std::string type = root.at("type").get<std::string>();

        if (type == "tool_call") {
            return parseToolCall(root);
        }

        if (type == "final_answer") {
            return parseFinalAnswer(root);
        }

        return AgentAction::failure(
            "unsupported LLM action type: " + type
        );
    } catch (const nlohmann::json::exception& error) {
        return AgentAction::failure(
            "invalid LLM planner JSON: "
            + std::string(error.what())
        );
    } catch (const std::exception& error) {
        return AgentAction::failure(
            "failed to parse LLM planner response: "
            + std::string(error.what())
        );
    }
}

std::string LlmAgentPlanner::extractJsonObject(const std::string & response) {
    const std::size_t begin = response.find('{');
    const std::size_t end = response.rfind('}');

    if (
        begin == std::string::npos
        || end == std::string::npos
        || end < begin
    ) {
        throw std::invalid_argument(
            "response does not contain a JSON object"
        );
    }

    return response.substr(begin, end - begin + 1);
}

AgentAction LlmAgentPlanner::parseToolCall(const nlohmann::json& root) const {
    if (
        !root.contains("tool_call")
        || !root.at("tool_call").is_object()
    ) {
        return AgentAction::failure(
            "tool_call action requires object field: tool_call"
        );
    }

    const nlohmann::json& call = root.at("tool_call");

    if (
        !call.contains("id")
        || !call.at("id").is_string()
        || call.at("id").get<std::string>().empty()
    ) {
        return AgentAction::failure(
            "tool call requires a non-empty string id"
        );
    }

    if (
        !call.contains("name")
        || !call.at("name").is_string()
        || call.at("name").get<std::string>().empty()
    ) {
        return AgentAction::failure(
            "tool call requires a non-empty string name"
        );
    }

    if (
        !call.contains("arguments")
        || !call.at("arguments").is_object()
    ) {
        return AgentAction::failure(
            "tool call arguments must be an object"
        );
    }

    const std::string id = call.at("id").get<std::string>();
    const std::string tool_name = call.at("name").get<std::string>();

    if (registry_.find(tool_name) == nullptr) {
        return AgentAction::failure(
            "LLM requested an unregistered tool: "
            + tool_name
        );
    }

    return AgentAction::toolCall(
        id,
        tool_name,
        call.at("arguments")
    );
}

AgentAction LlmAgentPlanner::parseFinalAnswer(const nlohmann::json& root) {
    if (
        !root.contains("answer")
        || !root.at("answer").is_string()
    ) {
        return AgentAction::failure(
            "final_answer action requires string field: answer"
        );
    }

    const std::string answer = root.at("answer").get<std::string>();

    if (answer.empty()) {
        return AgentAction::failure(
            "final answer must not be empty"
        );
    }

    return AgentAction::finalAnswer(answer);
}

bool LlmAgentPlanner::cancel() {
    return llm_backend_.cancel();
}

}   // namespace edge::agent
