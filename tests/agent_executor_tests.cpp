#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "agent/agent_executor.h"
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

struct AgentFixture {
    MockVehicleDevice device;
    ToolRegistry registry;
    RuleAgentPlanner planner;
    AgentExecutor executor;

    AgentFixture()
        : executor(planner, registry) {
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

void testEnvironmentQueryExecutesImmediately() {
    AgentFixture fixture;

    const AgentResponse response =
        fixture.executor.run(
            "session-1",
            "车内温度是多少"
        );

    expectTrue(
        response.ok,
        "environment query succeeds"
    );

    expectTrue(
        response.state == AgentResponseState::Completed,
        "environment query completes immediately"
    );

    expectTrue(
        response.executed_tool
            == "get_cabin_environment",
        "environment tool is executed"
    );

    expectTrue(
        response.answer.find("28.5") != std::string::npos,
        "answer contains temperature"
    );

    expectTrue(
        response.answer.find("60.0") != std::string::npos,
        "answer contains humidity"
    );
}

void testControlRequiresConfirmation() {
    AgentFixture fixture;

    const AgentResponse response =
        fixture.executor.run(
            "session-1",
            "打开空调"
        );

    expectTrue(
        response.ok,
        "control request is accepted"
    );

    expectTrue(
        response.state
            == AgentResponseState::WaitingForConfirmation,
        "control request waits for confirmation"
    );

    expectTrue(
        !fixture.device.airConditionerEnabled(),
        "device remains unchanged before confirmation"
    );
}

void testConfirmationExecutesControl() {
    AgentFixture fixture;

    fixture.executor.run(
        "session-1",
        "打开空调"
    );

    const AgentResponse response =
        fixture.executor.run(
            "session-1",
            "确认"
        );

    expectTrue(
        response.ok,
        "confirmed control succeeds"
    );

    expectTrue(
        fixture.device.airConditionerEnabled(),
        "confirmed control changes device state"
    );

    expectTrue(
        response.executed_tool
            == "set_air_conditioner",
        "confirmed request executes control tool"
    );
}

void testCancellationDoesNotExecuteControl() {
    AgentFixture fixture;

    fixture.executor.run(
        "session-1",
        "打开空调"
    );

    const AgentResponse response =
        fixture.executor.run(
            "session-1",
            "取消"
        );

    expectTrue(
        response.ok,
        "cancellation succeeds"
    );

    expectTrue(
        !fixture.device.airConditionerEnabled(),
        "cancelled request does not change state"
    );

    expectTrue(
        !fixture.executor.hasPendingAction("session-1"),
        "cancel clears pending action"
    );
}

void testSessionsAreIndependent() {
    AgentFixture fixture;

    fixture.executor.run(
        "session-a",
        "打开空调"
    );

    const AgentResponse other_session =
        fixture.executor.run(
            "session-b",
            "空调状态"
        );

    expectTrue(
        other_session.state
            == AgentResponseState::Completed,
        "another session is not blocked"
    );

    expectTrue(
        fixture.executor.hasPendingAction("session-a"),
        "original session retains pending action"
    );
}

void testInvalidToolArgumentsAreRejected() {
    AgentFixture fixture;

    AgentToolCall call;
    call.id = "invalid-1";
    call.name = "set_air_conditioner";
    call.arguments = {
        {"enabled", "yes"}
    };

    const AgentToolResult result =
        fixture.registry.execute(call);

    expectTrue(
        !result.ok,
        "invalid boolean argument is rejected"
    );
}

void testUnknownToolIsRejected() {
    AgentFixture fixture;

    AgentToolCall call;
    call.id = "unknown-1";
    call.name = "execute_shell";
    call.arguments = {
        {"command", "anything"}
    };

    const AgentToolResult result =
        fixture.registry.execute(call);

    expectTrue(
        !result.ok,
        "unregistered tool is rejected"
    );
}

void testDuplicateToolIsRejected() {
    MockVehicleDevice device;
    ToolRegistry registry;

    registry.registerTool(
        std::make_unique<
            GetCabinEnvironmentTool
        >(device)
    );

    bool exception_observed = false;

    try {
        registry.registerTool(
            std::make_unique<
                GetCabinEnvironmentTool
            >(device)
        );
    } catch (const std::invalid_argument&) {
        exception_observed = true;
    }

    expectTrue(
        exception_observed,
        "duplicate tool registration is rejected"
    );
}

void testDeviceFailureIsReported() {
    AgentFixture fixture;
    fixture.device.setControlFailure(true);

    fixture.executor.run(
        "session-1",
        "打开空调"
    );

    const AgentResponse response =
        fixture.executor.run(
            "session-1",
            "确认"
        );

    expectTrue(
        !response.ok,
        "device failure reaches agent response"
    );

    expectTrue(
        !fixture.device.airConditionerEnabled(),
        "failed device operation does not change state"
    );
}

void testExpiredConfirmationDoesNotExecute() {
    MockVehicleDevice device;
    ToolRegistry registry;
    RuleAgentPlanner planner;

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

    AgentClock::time_point current_time{};

    AgentExecutorConfig config;
    config.maximum_steps = 4;
    config.confirmation_timeout =
        std::chrono::milliseconds(30000);

    config.now = [&current_time]() {
        return current_time;
    };

    AgentExecutor executor(
        planner,
        registry,
        config
    );

    const AgentResponse first =
        executor.run(
            "timeout-session",
            "打开空调"
        );

    expectTrue(
        first.state
            == AgentResponseState::
                WaitingForConfirmation,
        "control waits for confirmation"
    );

    current_time += std::chrono::seconds(31);

    const AgentResponse confirmed =
        executor.run(
            "timeout-session",
            "确认"
        );

    expectTrue(
        confirmed.ok,
        "expired confirmation returns safe response"
    );

    expectTrue(
        confirmed.answer.find("超时")
            != std::string::npos,
        "expired confirmation reports timeout"
    );

    expectTrue(
        !device.airConditionerEnabled(),
        "expired confirmation does not execute control"
    );

    expectTrue(
        !executor.hasPendingAction(
            "timeout-session"
        ),
        "expired confirmation clears pending action"
    );
}

}  // namespace

int main() {
    testEnvironmentQueryExecutesImmediately();
    testControlRequiresConfirmation();
    testConfirmationExecutesControl();
    testCancellationDoesNotExecuteControl();
    testSessionsAreIndependent();
    testInvalidToolArgumentsAreRejected();
    testUnknownToolIsRejected();
    testDuplicateToolIsRejected();
    testDeviceFailureIsReported();
    testExpiredConfirmationDoesNotExecute();

    if (failed_count != 0) {
        std::cout
            << failed_count
            << " agent test(s) failed\n";
        return 1;
    }

    std::cout << "All agent tests passed\n";
    return 0;
}
