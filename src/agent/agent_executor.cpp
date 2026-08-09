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
    ToolRegistry& registry,
    AgentExecutorConfig config
)
    : planner_(planner)
    , registry_(registry)
    , config_(config) {
    if (config_.maximum_steps == 0) {
        throw std::invalid_argument(
            "agent maximum_steps must be greater than zero"
        );
    }

    if (config_.confirmation_timeout.count() <= 0) {
        throw std::invalid_argument(
            "agent confirmation timeout must be positive"
        );
    }

    if (!config_.now) {
        throw std::invalid_argument(
            "agent clock function must not be empty"
        );
    }
}

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

    AgentPlanningContext context;
    context.user_input = user_input;

    return continueExecution(session_id, std::move(context));
}

AgentResponse AgentExecutor::continueExecution(
    const std::string& session_id,
    AgentPlanningContext context
) {
    while (context.observations.size() < config_.maximum_steps) {
        const AgentAction action = planner_.plan(context);

        if (action.type == AgentActionType::Error) {
            return AgentResponse::failure(
                action.error.empty()
                    ? "agent planning failed"
                    : action.error
            );
        }

        if (action.type == AgentActionType::FinalAnswer) {
            std::string last_tool;
            nlohmann::json last_observation = nlohmann::json::object();

            if (!context.observations.empty()) {
                const AgentObservation& last = context.observations.back();

                last_tool = last.tool_call.name;
                last_observation = last.tool_result.data;
            }

            return AgentResponse::completed(
                action.answer,
                last_tool,
                last_observation,
                buildTrace(context)
            );
        }

        AgentTool* tool = registry_.find(action.tool_call.name);

        if (tool == nullptr) {
            return AgentResponse::failure(
                "planner requested an unregistered tool: "
                + action.tool_call.name
            );
        }

        if (tool->requiresConfirmation()) {
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);

                pending_actions_[session_id] = {
                    context,
                    action.tool_call,
                    config_.now()
                };
            }

            AgentResponse response = AgentResponse::waiting(
                buildConfirmationPrompt(action.tool_call)
            );

            response.trace = buildTrace(context);
            return response;
        }

        const AgentToolResult result = registry_.execute(action.tool_call);

        context.observations.push_back({
            action.tool_call,
            result
        });
    }

    AgentResponse response = AgentResponse::failure("agent exceeded maximum execution steps");

    response.trace = buildTrace(context);
    return response;
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
    bool expired = false;

    {
        std::lock_guard<std::mutex> lock(pending_mutex_);

        const auto it = pending_actions_.find(session_id);

        if (it == pending_actions_.end()) {
            return AgentResponse::failure(
                "pending action no longer exists"
            );
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            config_.now() - it->second.created_at
        );

        if (elapsed >= config_.confirmation_timeout) {
            pending_actions_.erase(it);
            expired = true;
        }
    }

    if (expired) {
        return AgentResponse::completed(
            "设备操作确认已超时，操作已自动取消。"
        );
    }

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

    PendingAction pending;

    {
        std::lock_guard<std::mutex> lock(pending_mutex_);

        const auto it = pending_actions_.find(session_id);
    
        if (it == pending_actions_.end()) {
            return AgentResponse::failure(
                "pending action no longer exists"
            );
        }

        pending = std::move(it->second);
        pending_actions_.erase(it);
    }

    const AgentToolResult result = registry_.execute(pending.tool_call);

    pending.context.observations.push_back({
        pending.tool_call,
        result
    });

    return continueExecution(
        session_id,
        std::move(pending.context)
    );
}

nlohmann::json AgentExecutor::buildTrace(const AgentPlanningContext& context) {
    nlohmann::json trace = nlohmann::json::array();

    for (const AgentObservation& observation : context.observations) {
        trace.push_back({
            {
                "tool_call",
                {
                    {"id", observation.tool_call.id},
                    {"name", observation.tool_call.name},
                    {"arguments", observation.tool_call.arguments}
                }
            },
            {
                "result",
                {
                    {"ok", observation.tool_result.ok},
                    {"data", observation.tool_result.data},
                    {"error", observation.tool_result.error}
                }
            }
        });
    }

    return trace;
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
            ? "检测到需要开启模拟空调并点亮指示灯，"
              "请回答“确认”或“取消”。"
            : "检测到需要关闭模拟空调并熄灭指示灯，"
              "请回答“确认”或“取消”。";
    }

    return "该操作将改变设备状态，"
           "请回答“确认”或“取消”。";
}

bool AgentExecutor::cancel(const std::string& session_id) {
    if (session_id.empty()) {
        return false;
    }

    bool pending_action_removed = false;

    {
        std::lock_guard<std::mutex> lock(pending_mutex_);

        pending_action_removed = pending_actions_.erase(session_id) > 0;
    }

    const bool planner_cancelled = planner_.cancel();

    return pending_action_removed || planner_cancelled;
}

}   // namespace edge::agent
