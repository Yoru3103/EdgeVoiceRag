#include <iostream>
#include <memory>
#include <string>

#include "agent/llm_agent_planner.h"
#include "agent/tool_registry.h"
#include "agent/vehicle_device.h"
#include "agent/vehicle_tools.h"
#include "llm_backend.h"

namespace {

using namespace edge::agent;

int failed_count = 0;

void expectTrue(
    bool condition,
    const std::string& name
) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }

    std::cout << "[FAIL] " << name << '\n';
    failed_count++;
}

class ScriptedLlmBackend final : public LlmBackend {
public:
    std::string response;
    std::string last_prompt;
    bool should_fail = false;

    std::string name() const override {
        return "scripted_llm";
    }

    LlmGenerationResult generate(
        const std::string& prompt
    ) override {
        last_prompt = prompt;

        if (should_fail) {
            return LlmGenerationResult::failure(
                "scripted failure"
            );
        }

        return LlmGenerationResult::success(response);
    }
};

struct PlannerFixture {
    MockVehicleDevice device;
    ToolRegistry registry;
    ScriptedLlmBackend llm;

    PlannerFixture() {
        registry.registerTool(
            std::make_unique<
                GetCabinEnvironmentTool
            >(device)
        );

        registry.registerTool(
            std::make_unique<
                GetAirConditionerStateTool
            >(device)
        );

        registry.registerTool(
            std::make_unique<
                SetAirConditionerTool
            >(device)
        );

        registry.registerTool(
            std::make_unique<
                CheckCabinTemperatureConditionTool
            >(device)
        );
    }
};

void testParsesEnvironmentToolCall() {
    PlannerFixture fixture;

    fixture.llm.response = R"({
        "type": "tool_call",
        "tool_call": {
            "id": "call-1",
            "name": "get_cabin_environment",
            "arguments": {}
        }
    })";

    LlmAgentPlanner planner(
        fixture.llm,
        fixture.registry
    );

    AgentPlanningContext context;
    context.user_input = "车里现在多少度";

    const AgentAction action = planner.plan(context);

    expectTrue(
        action.type == AgentActionType::ToolCall,
        "parse environment tool call"
    );

    expectTrue(
        action.tool_call.name
            == "get_cabin_environment",
        "select environment tool"
    );

    expectTrue(
        fixture.llm.last_prompt.find(
            "get_cabin_environment"
        ) != std::string::npos,
        "prompt contains tool definition"
    );
}

void testParsesControlToolCall() {
    PlannerFixture fixture;

    fixture.llm.response = R"({
        "type": "tool_call",
        "tool_call": {
            "id": "call-2",
            "name": "set_air_conditioner",
            "arguments": {
                "enabled": true
            }
        }
    })";

    LlmAgentPlanner planner(
        fixture.llm,
        fixture.registry
    );

    AgentPlanningContext context;
    context.user_input = "打开空调";

    const AgentAction action = planner.plan(context);

    expectTrue(
        action.type == AgentActionType::ToolCall,
        "parse control tool call"
    );

    expectTrue(
        action.tool_call.arguments.at("enabled")
            .get<bool>(),
        "parse control argument"
    );
}

void testParsesFinalAnswer() {
    PlannerFixture fixture;

    fixture.llm.response = R"({
        "type": "final_answer",
        "answer": "当前设备无法完成导航操作。"
    })";

    LlmAgentPlanner planner(
        fixture.llm,
        fixture.registry
    );


    AgentPlanningContext context;
    context.user_input = "导航到公司";

    const AgentAction action = planner.plan(context);

    expectTrue(
        action.type == AgentActionType::FinalAnswer,
        "parse final answer"
    );

    expectTrue(
        action.answer.find("无法完成")
            != std::string::npos,
        "preserve final answer"
    );
}

void testRejectsUnknownTool() {
    PlannerFixture fixture;

    fixture.llm.response = R"({
        "type": "tool_call",
        "tool_call": {
            "id": "call-3",
            "name": "execute_shell",
            "arguments": {
                "command": "anything"
            }
        }
    })";

    LlmAgentPlanner planner(
        fixture.llm,
        fixture.registry
    );


    AgentPlanningContext context;
    context.user_input = "执行命令";

    const AgentAction action = planner.plan(context);

    expectTrue(
        action.type == AgentActionType::Error,
        "reject unknown tool"
    );

    expectTrue(
        action.error.find("unregistered")
            != std::string::npos,
        "report unknown tool error"
    );
}

void testRejectsMalformedArguments() {
    PlannerFixture fixture;

    fixture.llm.response = R"({
        "type": "tool_call",
        "tool_call": {
            "id": "call-4",
            "name": "set_air_conditioner",
            "arguments": true
        }
    })";

    LlmAgentPlanner planner(
        fixture.llm,
        fixture.registry
    );


    AgentPlanningContext context;
    context.user_input = "打开空调";

    const AgentAction action = planner.plan(context);

    expectTrue(
        action.type == AgentActionType::Error,
        "reject non-object arguments"
    );
}

void testAcceptsMarkdownWrappedJson() {
    PlannerFixture fixture;

    fixture.llm.response = R"(
    ```json
        {
            "type": "final_answer",
            "answer": "测试回答"
        }
    )";

    LlmAgentPlanner planner(
        fixture.llm,
        fixture.registry
    );


    AgentPlanningContext context;
    context.user_input = "测试";

    const AgentAction action = planner.plan(context);

    expectTrue(
        action.type == AgentActionType::FinalAnswer,
        "extract JSON from markdown wrapper"
    );
}

void testReportsLlmFailure() {
    PlannerFixture fixture;
    fixture.llm.should_fail = true;

    LlmAgentPlanner planner(
        fixture.llm,
        fixture.registry
    );


    AgentPlanningContext context;
    context.user_input = "打开空调";

    const AgentAction action = planner.plan(context);

    expectTrue(
        action.type == AgentActionType::Error,
        "report LLM generation failure"
    );

    expectTrue(
        action.error.find("scripted failure")
            != std::string::npos,
        "retain LLM failure reason"
    );
}

void testRejectsInvalidJson() {
    PlannerFixture fixture;
    fixture.llm.response = "这不是JSON";

    LlmAgentPlanner planner(
        fixture.llm,
        fixture.registry
    );


    AgentPlanningContext context;
    context.user_input = "打开空调";

    const AgentAction action = planner.plan(context);

    expectTrue(
        action.type == AgentActionType::Error,
        "reject invalid JSON"
    );
}

void testParsesTemperatureConditionToolCall() {
    PlannerFixture fixture;

    fixture.llm.response = R"({
        "type": "tool_call",
        "tool_call": {
            "id": "condition-check",
            "name": "check_cabin_temperature_condition",
            "arguments": {
                "operator": "gt",
                "threshold_c": 27.0
            }
        }
    })";

    LlmAgentPlanner planner(
        fixture.llm,
        fixture.registry
    );

    AgentPlanningContext context;
    context.user_input =
        "座舱超过二十七度时开启制冷";

    const AgentAction action =
        planner.plan(context);

    expectTrue(
        action.type == AgentActionType::ToolCall,
        "parse temperature condition tool call"
    );

    expectTrue(
        action.tool_call.name
            == "check_cabin_temperature_condition",
        "select deterministic condition tool"
    );

    expectTrue(
        action.tool_call.arguments
                .at("operator")
                .get<std::string>()
            == "gt",
        "parse condition operator"
    );

    expectTrue(
        action.tool_call.arguments
                .at("threshold_c")
                .get<double>()
            == 27.0,
        "parse condition threshold"
    );

    expectTrue(
        fixture.llm.last_prompt.find(
            "只根据Observation中的matched字段"
        ) != std::string::npos,
        "prompt requires matched-only decision"
    );

    const std::size_t check_position =
        fixture.llm.last_prompt.find(
            "\"check_cabin_temperature_condition\""
        );

    const std::size_t state_position =
        fixture.llm.last_prompt.find(
            "\"get_air_conditioner_state\""
        );

    const std::size_t environment_position =
        fixture.llm.last_prompt.find(
            "\"get_cabin_environment\""
        );

    const std::size_t control_position =
        fixture.llm.last_prompt.find(
            "\"set_air_conditioner\""
        );

    expectTrue(
        check_position < state_position
            && state_position < environment_position
            && environment_position < control_position,
        "tool definitions use deterministic order"
    );
}

}   // namespace

int main() {
    testParsesEnvironmentToolCall();
    testParsesControlToolCall();
    testParsesFinalAnswer();
    testRejectsUnknownTool();
    testRejectsMalformedArguments();
    testAcceptsMarkdownWrappedJson();
    testReportsLlmFailure();
    testRejectsInvalidJson();
    testParsesTemperatureConditionToolCall();

        if (failed_count != 0) {
        std::cout
            << failed_count
            << " LLM planner test(s) failed\n";
        return 1;
    }

    std::cout
        << "All LLM agent planner tests passed\n";

    return 0;
}
