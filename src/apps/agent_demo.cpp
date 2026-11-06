#include <iostream>
#include <memory>
#include <string>

#include "agent/agent_executor.h"
#include "agent/rule_agent_planner.h"
#include "agent/tool_registry.h"
#include "agent/vehicle_device.h"
#include "agent/vehicle_tools.h"

int main() {
    using namespace edge::agent;

    MockVehicleDevice device;
    device.setEnvironment(28.5F, 60.0F);

    ToolRegistry registry;

    registry.registerTool(
        std::make_unique<GetCabinEnvironmentTool>(device)
    );
    registry.registerTool(
        std::make_unique<CheckCabinTemperatureConditionTool>(device)
    );
    registry.registerTool(
        std::make_unique<SetAirConditionerIfTemperatureTool>(device)
    );
    registry.registerTool(
        std::make_unique<GetAirConditionerStateTool>(device)
    );
    registry.registerTool(
        std::make_unique<SetAirConditionerTool>(device)
    );

    RuleAgentPlanner planner;
    AgentExecutor executor(planner, registry);

    const std::string session_id = "console-session";
    std::cout
        << "Edge Vehicle Agent Demo\n"
        << "可输入：\n"
        << "  车内温度是多少\n"
        << "  空调状态\n"
        << "  打开空调\n"
        << "  关闭空调\n"
        << "  确认\n"
        << "  取消\n"
        << "  exit\n\n";

    while (true) {
        std::cout << "用户> ";

        std::string input;
        if (!std::getline(std::cin, input)) {
            break;
        }

        if (input == "exit" || input == "退出") {
            break;
        }

        const AgentResponse response =
            executor.run(session_id, input);

        if (!response.ok) {
            std::cout
                << "Agent error> "
                << response.error
                << "\n";
            continue;
        }

        std::cout
            << "Agent> "
            << response.answer
            << "\n";

        if (!response.executed_tool.empty()) {
            std::cout
                << "Tool> "
                << response.executed_tool
                << "\n";

            std::cout
                << "Observation> "
                << response.observation.dump()
                << "\n";
        }
    }

    return 0;
}
