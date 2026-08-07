#include "agent/agent_executor.h"

#include <iomanip>
#include <sstream>
#include <utility>

namespace edge::agent {

namespace {

std::string formatNumber(float value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    return stream.str();
}

}   // namespace

AgentExecutor::AgentExecutor(
    AgentPlanner& planner,
    ToolRegistry& registry
)
    : planner_(planner)
    , registry_(registry) {}

AgentResponse AgentExecutor::run(
    const std::string& session_id,
    const std::string& user_input
) {
    if (session_id.empty()) {
        return AgentResponse::failure(
            "session_id must not be empty"
        );
    }

    if (user_input.empty()) {
        return AgentResponse::failure(
            "user input must not be empty"
        );
    }

    if (hasPendingAction(session_id)) {
        return handlePendingAction(
            session_id,
            user_input
        );
    }

    const AgentAction action = planner_.plan(user_input);

    return processAction(session_id, action);
}

bool AgentExecutor::hasPendingAction(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(pending_mutex_);

    return pending_actions_.find(session_id) != pending_actions_.end();
}

void AgentExecutor::clearSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_actions_.erase(session_id);
}

AgentResponse AgentExecutor::handlePendingAction(
    const std::string& session_id,
    const std::string& user_input
) {
    if (isCancellation(user_input)) {
        clearSession(session_id);

        return AgentResponse::completed(
            "已取消设备操作。"
        );
    }

    if (!isConfirmation(user_input)) {
        return AgentResponse::waiting(
            "当前有一项待确认的设备操作，"
            "请回答“确认”或“取消”。"
        );
    }

    AgentToolCall call;

    {
        std::lock_guard<std::mutex> lock(pending_mutex_);

        const auto it = pending_actions_.find(session_id);

        if (it == pending_actions_.end()) {
            return AgentResponse::failure(
                "pending action no longer exists"
            );
        }

        call = it->second.tool_call;
        pending_actions_.erase(it);
    }

    return executeTool(call);
}

AgentResponse AgentExecutor::processAction(
    const std::string& session_id,
    const AgentAction& action
) {
    switch (action.type) {
        case AgentActionType::FinalAnswer:
            return AgentResponse::completed(action.answer);

        case AgentActionType::Error:
            return AgentResponse::failure(action.error.empty() ? "agent planning failed" : action.error);

        case AgentActionType::ToolCall:
            break;
    }

    const AgentTool* tool = registry_.find(action.tool_call.name);

    if (tool == nullptr) {
        return AgentResponse::failure(
            "planner requested an unregistered tool: "
            + action.tool_call.name
        );
    }

    if (tool->requiresConfirmation()) {
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);

            pending_actions_[session_id] = {action.tool_call};
        }

        return AgentResponse::waiting(
            buildConfirmationPrompt(action.tool_call)
        );
    }

    return executeTool(action.tool_call);
}

AgentResponse AgentExecutor::executeTool(const AgentToolCall& call) {
    const AgentToolResult result = registry_.execute(call);

    return renderToolResult(call, result);
}

AgentResponse AgentExecutor::renderToolResult(
    const AgentToolCall& call,
    const AgentToolResult& result
) {
    if (!result.ok) {
        return AgentResponse::failure(
            "工具 " + call.name
            + " 执行失败：" + result.error
        );
    }

    if (call.name == "get_cabin_environment") {
        if (
            !result.data.contains("temperature_c")
            || !result.data.contains("humidity_percent")
        ) {
            return AgentResponse::failure(
                "environment observation is incomplete"
            );
        }

        const float temperature =
            result.data.at("temperature_c").get<float>();

        const float humidity =
            result.data.at("humidity_percent").get<float>();

        return AgentResponse::completed(
            "当前车内温度为"
                + formatNumber(temperature)
                + "摄氏度，湿度为"
                + formatNumber(humidity)
                + "%。",
            call.name,
            result.data
        );
    }

    if (call.name == "get_air_conditioner_state") {
        const bool enabled =
            result.data.at("enabled").get<bool>();

        return AgentResponse::completed(
            enabled
                ? "模拟空调当前处于开启状态，"
                  "指示灯已点亮。"
                : "模拟空调当前处于关闭状态，"
                  "指示灯已熄灭。",
            call.name,
            result.data
        );
    }

    if (call.name == "set_air_conditioner") {
        const bool enabled =
            result.data.at("enabled").get<bool>();

        return AgentResponse::completed(
            enabled
                ? "模拟空调已开启，指示灯已点亮。"
                : "模拟空调已关闭，指示灯已熄灭。",
            call.name,
            result.data
        );
    }

    return AgentResponse::completed(
        "工具执行成功。",
        call.name,
        result.data
    );
}

bool AgentExecutor::isConfirmation(const std::string& text) {
    return
        text == "确认"
        || text == "确定"
        || text == "同意"
        || text == "执行"
        || text == "是";
}

bool AgentExecutor::isCancellation(const std::string& text) {
    return
        text == "取消"
        || text == "不要"
        || text == "否"
        || text == "停止";
}

std::string AgentExecutor::buildConfirmationPrompt(const AgentToolCall& call) {
    if (call.name == "set_air_conditioner") {
        const bool enabled =
            call.arguments.value("enabled", false);

        return enabled
            ? "即将开启模拟空调并点亮指示灯，"
              "请回答“确认”或“取消”。"
            : "即将关闭模拟空调并熄灭指示灯，"
              "请回答“确认”或“取消”。";
    }

    return "该操作将改变设备状态，"
           "请回答“确认”或“取消”。";
}

}   // namespace edge::agent
