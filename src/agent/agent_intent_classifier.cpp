#include "agent/agent_intent_classifier.h"

namespace edge::agent {

AgentIntentClassification AgentIntentClassifier::classify(const std::string& query) const {
    if (query.empty()) {
        return {};
    }

    const bool contains_air_conditioner = contains(query, "空调");

    const bool contains_environment = containsAny(
        query,
        {
            "车内温度",
            "车内的温度",
            "车里温度",
            "车里的温度",
            "车内湿度",
            "车内的湿度",
            "车里湿度",
            "车里的湿度",
            "温度是多少",
            "湿度是多少",
            "多少度",
            "热不热",
            "温度超过",
            "温度高于",
            "温度低于"
        }
    );

    const bool contains_control =
        containsAny(
            query,
            {
                "打开空调",
                "开启空调",
                "把空调打开",
                "关闭空调",
                "关掉空调",
                "把空调关掉"
            }
        );

    const bool contains_condition =
        containsAny(
            query,
            {
                "如果",
                "超过",
                "高于",
                "低于",
                "达到",
                "就"
            }
        );

    /*
     * 条件任务优先识别。
     *
     * 例如：
     * 如果车内温度超过26度，就打开空调。
     */
    if (
        contains_environment
        && contains_condition
        && contains_control
    ) {
        return {
            AgentIntent::ConditionalWorkflow,
            0.95F,
            "query contains device condition and control target"
        };
    }

    /*
     * “空调怎么打开”是在询问方法，不是要求立即执行。
     * 这类问题应该进入RAG查询车辆手册。
     */
    if (isInstructionQuestion(query)) {
        return {
            AgentIntent::None,
            0.0F,
            "instruction question should use vehicle knowledge"
        };
    }

    if (contains_environment) {
        return {
            AgentIntent::EnvironmentQuery,
            0.90F,
            "query requests live cabin environment data"
        };
    }

    if (
        contains_air_conditioner
        && containsAny(
            query,
            {
                "状态",
                "开了吗",
                "关了吗",
                "是否开启",
                "有没有开",
                "现在开着吗"
            }
        )
    ) {
        return {
            AgentIntent::DeviceStateQuery,
            0.90F,
            "query requests live device state"
        };
    }

    if (contains_control) {
        return {
            AgentIntent::DeviceControl,
            0.95F,
            "query requests a device state change"
        };
    }

    return {
        AgentIntent::None,
        0.0F,
        "query does not require device tools"
    };
}

std::string AgentIntentClassifier::intentToString(
    AgentIntent intent
) {
    switch (intent) {
        case AgentIntent::EnvironmentQuery:
            return "environment_query";

        case AgentIntent::DeviceStateQuery:
            return "device_state_query";

        case AgentIntent::DeviceControl:
            return "device_control";

        case AgentIntent::ConditionalWorkflow:
            return "conditional_workflow";

        case AgentIntent::None:
        default:
            return "none";
    }
}

bool AgentIntentClassifier::contains(
    const std::string& text,
    const std::string& keyword
) {
    return text.find(keyword) != std::string::npos;
}

bool AgentIntentClassifier::containsAny(
    const std::string& text,
    const std::vector<std::string>& keywords
) {
    for (const std::string& keyword : keywords) {
        if (contains(text, keyword)) {
            return true;
        }
    }

    return false;
}

bool AgentIntentClassifier::isInstructionQuestion(
    const std::string& query
) {
    return containsAny(
        query,
        {
            "怎么",
            "如何",
            "怎样",
            "方法",
            "步骤",
            "教程",
            "在哪里",
            "在哪儿",
            "不知道"
        }
    );
}

}   // namespace edge::agent
