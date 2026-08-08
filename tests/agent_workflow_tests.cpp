#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "agent/agent_executor.h"
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

class SequenceLlmBackend final : public LlmBackend {
public:
    std::vector<std::string> responses;
    std::vector<std::string> prompts;
    std::size_t current = 0;

    std::string name() const override {
        return "sequence_llm";
    }

    LlmGenerationResult generate(
        const std::string& prompt
    ) override {
        prompts.push_back(prompt);

        if (current >= responses.size()) {
            return LlmGenerationResult::failure(
                "no scripted LLM response remains"
            );
        }

        return LlmGenerationResult::success(
            responses[current++]
        );
    }
};

struct WorkflowFixture {
    MockVehicleDevice device;
    ToolRegistry registry;
    SequenceLlmBackend llm;

    WorkflowFixture() {
        device.setEnvironment(28.5F, 60.0F);

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
    }
};

void testConditionalControlWorkflow() {
    WorkflowFixture fixture;

    fixture.llm.responses = {
        R"({
            "type": "tool_call",
            "tool_call": {
                "id": "call-temperature",
                "name": "get_cabin_environment",
                "arguments": {}
            }
        })",

        R"({
            "type": "tool_call",
            "tool_call": {
                "id": "call-air-conditioner",
                "name": "set_air_conditioner",
                "arguments": {
                    "enabled": true
                }
            }
        })",

        R"({
            "type": "final_answer",
            "answer": "车内温度为28.5摄氏度，超过26度，模拟空调已经开启。"
        })"
    };

    LlmAgentPlanner planner(
        fixture.llm,
        fixture.registry
    );

    AgentExecutor executor(
        planner,
        fixture.registry,
        {.maximum_steps = 4}
    );

    const AgentResponse first =
        executor.run(
            "workflow-1",
            "如果车内温度超过26度，就打开空调"
        );

    expectTrue(
        first.ok,
        "conditional workflow starts"
    );

    expectTrue(
        first.state
            == AgentResponseState::WaitingForConfirmation,
        "workflow waits before write operation"
    );

    expectTrue(
        !fixture.device.airConditionerEnabled(),
        "air conditioner remains off before confirmation"
    );

    expectTrue(
        first.trace.size() == 1,
        "temperature observation is recorded"
    );

    expectTrue(
        fixture.llm.prompts.at(1).find("28.5")
            != std::string::npos,
        "second planning prompt contains observation"
    );

    const AgentResponse confirmed =
        executor.run(
            "workflow-1",
            "确认"
        );

    expectTrue(
        confirmed.ok,
        "confirmed workflow succeeds"
    );

    expectTrue(
        confirmed.state
            == AgentResponseState::Completed,
        "confirmed workflow completes"
    );

    expectTrue(
        fixture.device.airConditionerEnabled(),
        "air conditioner is enabled"
    );

    expectTrue(
        confirmed.trace.size() == 2,
        "complete workflow records two tool calls"
    );

    expectTrue(
        confirmed.answer.find("已经开启")
            != std::string::npos,
        "final answer reports execution result"
    );
}

void testConditionDoesNotRequireControl() {
    WorkflowFixture fixture;
    fixture.device.setEnvironment(23.0F, 50.0F);

    fixture.llm.responses = {
        R"({
            "type": "tool_call",
            "tool_call": {
                "id": "call-temperature",
                "name": "get_cabin_environment",
                "arguments": {}
            }
        })",

        R"({
            "type": "final_answer",
            "answer": "车内温度为23度，没有超过26度，因此不需要开启空调。"
        })"
    };

    LlmAgentPlanner planner(
        fixture.llm,
        fixture.registry
    );

    AgentExecutor executor(
        planner,
        fixture.registry
    );

    const AgentResponse response =
        executor.run(
            "workflow-2",
            "如果车内温度超过26度，就打开空调"
        );

    expectTrue(
        response.ok,
        "low-temperature workflow succeeds"
    );

    expectTrue(
        response.state
            == AgentResponseState::Completed,
        "low-temperature workflow completes directly"
    );

    expectTrue(
        !fixture.device.airConditionerEnabled(),
        "condition prevents control operation"
    );

    expectTrue(
        response.trace.size() == 1,
        "only sensor tool is executed"
    );
}

void testMaximumStepsStopsLoop() {
    WorkflowFixture fixture;

    const std::string repeated_call = R"({
        "type": "tool_call",
        "tool_call": {
            "id": "repeated-call",
            "name": "get_cabin_environment",
            "arguments": {}
        }
    })";

    fixture.llm.responses = {
        repeated_call,
        repeated_call,
        repeated_call,
        repeated_call
    };

    LlmAgentPlanner planner(
        fixture.llm,
        fixture.registry
    );

    AgentExecutor executor(
        planner,
        fixture.registry,
        {.maximum_steps = 3}
    );

    const AgentResponse response =
        executor.run(
            "workflow-loop",
            "持续查询温度"
        );

    expectTrue(
        !response.ok,
        "maximum steps stops repeated planning"
    );

    expectTrue(
        response.error.find("maximum")
            != std::string::npos,
        "maximum step error is reported"
    );

    expectTrue(
        response.trace.size() == 3,
        "trace contains maximum number of steps"
    );
}

}  // namespace

int main() {
    testConditionalControlWorkflow();
    testConditionDoesNotRequireControl();
    testMaximumStepsStopsLoop();

    if (failed_count != 0) {
        std::cout
            << failed_count
            << " workflow test(s) failed\n";
        return 1;
    }

    std::cout
        << "All agent workflow tests passed\n";

    return 0;
}
