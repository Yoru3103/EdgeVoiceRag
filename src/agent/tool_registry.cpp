#include "agent/tool_registry.h"

#include <stdexcept>
#include <utility>

namespace edge::agent {

void ToolRegistry::registerTool(std::unique_ptr<AgentTool> tool) {
    if (!tool) {
        throw std::invalid_argument(
            "agent tool must not be null"
        );
    }

    const std::string tool_name = tool->name();

    if (tool_name.empty()) {
        throw std::invalid_argument(
            "agent tool name must not be empty"
        );
    }

    if (tools_.find(tool_name) != tools_.end()) {
        throw std::invalid_argument(
            "duplicate agent tool: " + tool_name
        );
    }

    tools_.emplace(tool_name, std::move(tool));
}

AgentTool* ToolRegistry::find(const std::string& name) {
    const auto it = tools_.find(name);

    if (it == tools_.end()) {
        return nullptr;
    }

    return it->second.get();
}

const AgentTool* ToolRegistry::find(const std::string& name) const {
    const auto iterator = tools_.find(name);

    if (iterator == tools_.end()) {
        return nullptr;
    }

    return iterator->second.get();
}

AgentToolResult ToolRegistry::execute(const AgentToolCall& call) {
    AgentTool* tool = find(call.name);

    if (tool == nullptr) {
        return AgentToolResult::failure(
            "unknown agent tool: " + call.name
        );
    }

    try {
        return tool->execute(call.arguments);
    } catch (const std::exception& error) {
        return AgentToolResult::failure(
            "tool execution exception: "
            + std::string(error.what())
        );
    }
}

nlohmann::json ToolRegistry::definition() const {
    nlohmann::json result = nlohmann::json::array();

    for (const auto& [name, tool] : tools_) {
        result.push_back({
            {"name", name},
            {"description", tool->description()},
            {"parameters", tool->parametersSchema()},
            {"requires_confirmation", tool->requiresConfirmation()}
        });
    }

    return result;
}

}   // namespace edge::agent
