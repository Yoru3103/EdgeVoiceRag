#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "agent/agent_answer_backend.h"
#include "agent/rule_agent_planner.h"
#include "agent/tool_registry.h"
#include "agent/vehicle_device.h"
#include "agent/vehicle_tools.h"

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

struct Fixture {
    MockVehicleDevice device;
    ToolRegistry registry;
    RuleAgentPlanner planner;
    AgentExecutor executor;
    AgentAnswerBackend backend;

    Fixture()
        : executor(planner, registry)
        , backend(executor) {
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

void testEnvironmentQueryUsesStreamProtocol() {
    Fixture fixture;
    std::vector<RagStreamEvent> events;

    const RagStreamQueryResult result =
        fixture.backend.query(
            {"agent-1", "车内温度是多少"},
            [&events](const RagStreamEvent& event) {
                events.push_back(event);
            }
        );

    expectTrue(
        result.ok,
        "agent backend query succeeds"
    );

    expectTrue(
        result.response_mode == "agent_tool",
        "completed workflow uses agent_tool mode"
    );

    expectTrue(
        result.answer.find("28.5")
            != std::string::npos,
        "agent answer contains temperature"
    );

    expectTrue(
        events.size() == 2,
        "agent emits chunk and finished events"
    );

    expectTrue(
        events.at(0).type
            == RagStreamEventType::Chunk,
        "first event is chunk"
    );

    expectTrue(
        events.at(1).type
            == RagStreamEventType::Finished,
        "second event is finished"
    );
}

void testConfirmationSurvivesRequestIds() {
    Fixture fixture;

    const RagStreamQueryResult first =
        fixture.backend.query(
            {"agent-2", "打开空调"}
        );

    expectTrue(
        first.ok,
        "control request succeeds"
    );

    expectTrue(
        first.response_mode
            == "agent_confirmation",
        "control waits for confirmation"
    );

    expectTrue(
        fixture.backend.hasPendingAction(),
        "backend retains pending action"
    );

    const RagStreamQueryResult second =
        fixture.backend.query(
            {"agent-3", "确认"}
        );

    expectTrue(
        second.ok,
        "confirmation succeeds with new request id"
    );

    expectTrue(
        fixture.device.airConditionerEnabled(),
        "confirmation changes device state"
    );

    expectTrue(
        !fixture.backend.hasPendingAction(),
        "pending action is cleared"
    );
}

void testCancelClearsPendingAction() {
    Fixture fixture;

    fixture.backend.query(
        {"agent-4", "打开空调"}
    );

    const CancellationResult cancelled =
        fixture.backend.cancel("agent-4");

    expectTrue(
        cancelled.ok,
        "agent cancellation succeeds"
    );

    expectTrue(
        cancelled.cancelled,
        "pending action is cancelled"
    );

    expectTrue(
        !fixture.backend.hasPendingAction(),
        "cancel clears pending action"
    );

    expectTrue(
        !fixture.device.airConditionerEnabled(),
        "cancel does not execute device control"
    );
}

void testRejectsEmptyRequest() {
    Fixture fixture;

    const RagStreamQueryResult result =
        fixture.backend.query(
            {"", "车内温度是多少"}
        );

    expectTrue(
        !result.ok,
        "empty request id is rejected"
    );
}

}  // namespace

int main() {
    testEnvironmentQueryUsesStreamProtocol();
    testConfirmationSurvivesRequestIds();
    testCancelClearsPendingAction();
    testRejectsEmptyRequest();

    if (failed_count != 0) {
        std::cout
            << failed_count
            << " agent backend test(s) failed\n";
        return 1;
    }

    std::cout
        << "All agent answer backend tests passed\n";

    return 0;
}
