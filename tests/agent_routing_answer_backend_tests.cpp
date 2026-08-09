#include <iostream>
#include <memory>
#include <string>

#include "agent/agent_answer_backend.h"
#include "agent/agent_routing_answer_backend.h"
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

class RecordingFallback final
    : public StreamingAnswerBackend
    , public CancellableAnswerBackend {
public:
    mutable int query_count = 0;
    mutable int cancel_count = 0;

    std::string name() const override {
        return "recording_fallback";
    }

    RagStreamQueryResult query(
        const RagStreamRequest& request,
        const RagStreamEventHandler& handler = {}
    ) const override {
        query_count++;

        const std::string answer =
            "fallback answer";

        if (handler) {
            RagStreamEvent chunk;
            chunk.type = RagStreamEventType::Chunk;
            chunk.ok = true;
            chunk.request_id = request.request_id;
            chunk.sequence = 0;
            chunk.delta = answer;
            chunk.backend = name();
            handler(chunk);

            RagStreamEvent finished;
            finished.type = RagStreamEventType::Finished;
            finished.ok = true;
            finished.request_id = request.request_id;
            finished.sequence = 1;
            finished.answer = answer;
            finished.backend = name();
            finished.finished = true;
            handler(finished);
        }

        RagStreamQueryResult result =
            RagStreamQueryResult::success(
                request.request_id,
                answer,
                name(),
                "",
                0.0
            );

        result.response_mode = "fallback";
        return result;
    }

    CancellationResult cancel(
        const std::string& request_id
    ) const override {
        cancel_count++;

        return CancellationResult::success(
            request_id,
            true,
            request_id
        );
    }
};

struct Fixture {
    MockVehicleDevice device;
    ToolRegistry registry;
    RuleAgentPlanner planner;
    AgentExecutor executor;
    AgentAnswerBackend agent_backend;
    RecordingFallback fallback;
    AgentRoutingAnswerBackend router;

    Fixture()
        : executor(planner, registry)
        , agent_backend(executor)
        , router(
            agent_backend,
            fallback,
            fallback
        ) {
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

void testEnvironmentUsesAgent() {
    Fixture fixture;

    const RagStreamQueryResult result =
        fixture.router.query({
            "route-1",
            "车内温度是多少"
        });

    expectTrue(
        result.ok,
        "environment route succeeds"
    );

    expectTrue(
        result.response_mode == "agent_tool",
        "environment uses agent"
    );

    expectTrue(
        fixture.fallback.query_count == 0,
        "environment skips fallback"
    );
}

void testControlUsesAgent() {
    Fixture fixture;

    const RagStreamQueryResult result =
        fixture.router.query({
            "route-2",
            "打开空调"
        });

    expectTrue(
        result.response_mode
            == "agent_confirmation",
        "control uses agent confirmation"
    );

    expectTrue(
        !fixture.device.airConditionerEnabled(),
        "control does not execute before confirmation"
    );
}

void testConfirmationContinuesAgent() {
    Fixture fixture;

    fixture.router.query({
        "route-3",
        "打开空调"
    });

    const RagStreamQueryResult result =
        fixture.router.query({
            "route-4",
            "确认"
        });

    expectTrue(
        result.ok,
        "confirmation succeeds"
    );

    expectTrue(
        fixture.device.airConditionerEnabled(),
        "confirmation executes pending action"
    );

    expectTrue(
        result.query_category
            == "agent_continuation",
        "confirmation is marked as continuation"
    );
}

void testManualQuestionUsesFallback() {
    Fixture fixture;

    const RagStreamQueryResult result =
        fixture.router.query({
            "route-5",
            "空调怎么打开"
        });

    expectTrue(
        result.response_mode == "fallback",
        "instruction question uses fallback"
    );

    expectTrue(
        fixture.fallback.query_count == 1,
        "fallback receives instruction question"
    );
}

void testOrdinaryQuestionUsesFallback() {
    Fixture fixture;

    fixture.router.query({
        "route-6",
        "给我讲个故事"
    });

    expectTrue(
        fixture.fallback.query_count == 1,
        "ordinary query uses fallback"
    );
}

void testEmergencyOverridesPendingAction() {
    Fixture fixture;

    fixture.router.query({
        "route-7",
        "打开空调"
    });

    expectTrue(
        fixture.agent_backend.hasPendingAction(),
        "control creates pending action"
    );

    const RagStreamQueryResult result =
        fixture.router.query({
            "route-8",
            "制动系统故障，非常危险"
        });

    expectTrue(
        result.response_mode == "fallback",
        "emergency uses safety fallback"
    );

    expectTrue(
        !fixture.agent_backend.hasPendingAction(),
        "emergency clears stale pending action"
    );

    expectTrue(
        !fixture.device.airConditionerEnabled(),
        "emergency does not execute stale control"
    );
}

}  // namespace

int main() {
    testEnvironmentUsesAgent();
    testControlUsesAgent();
    testConfirmationContinuesAgent();
    testManualQuestionUsesFallback();
    testOrdinaryQuestionUsesFallback();
    testEmergencyOverridesPendingAction();

    if (failed_count != 0) {
        std::cout
            << failed_count
            << " agent routing test(s) failed\n";
        return 1;
    }

    std::cout
        << "All agent routing tests passed\n";

    return 0;
}
