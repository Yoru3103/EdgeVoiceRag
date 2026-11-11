#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "agent/agent_executor.h"
#include "agent/linux_vehicle_device.h"
#include "agent/rule_agent_planner.h"
#include "agent/tool_registry.h"
#include "agent/vehicle_device.h"
#include "agent/vehicle_tools.h"

namespace {

struct DemoOptions {
    std::string device = "mock";
    std::string iio_root = "/sys/bus/iio/devices";
    std::string iio_device_name = "edge_dht11";
    bool help = false;
};

void printUsage(std::ostream& output) {
    output
        << "Usage: agent_demo [--device mock|iio]\n"
        << "                  [--iio-root DIR] [--iio-device-name NAME]\n"
        << "       agent_demo --help\n\n"
        << "默认使用Mock设备；iio模式读取真实Linux IIO温湿度。\n"
        << "两种模式均使用Rule Planner，不加载ASR/TTS/LLM模型。\n"
        << "空调/LED当前仍为进程内模拟状态，写操作需要确认。\n";
}

DemoOptions parseOptions(int argc, char* argv[]) {
    DemoOptions options;
    bool has_iio_option = false;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            options.help = true;
            continue;
        }
        if (argument != "--device" && argument != "--iio-root"
            && argument != "--iio-device-name") {
            throw std::invalid_argument("unknown option: " + argument);
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + argument);
        }
        const std::string value = argv[++index];
        if (value.empty() || value.rfind("--", 0) == 0) {
            throw std::invalid_argument("missing value for " + argument);
        }
        if (argument == "--device") {
            options.device = value;
        } else if (argument == "--iio-root") {
            options.iio_root = value;
            has_iio_option = true;
        } else {
            options.iio_device_name = value;
            has_iio_option = true;
        }
    }

    if (options.device != "mock" && options.device != "iio") {
        throw std::invalid_argument("--device must be mock or iio");
    }
    if (has_iio_option && options.device != "iio") {
        throw std::invalid_argument("IIO options require --device iio");
    }
    return options;
}

}  // namespace

int main(int argc, char* argv[]) {
    using namespace edge::agent;

    DemoOptions options;
    try {
        options = parseOptions(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "Invalid arguments: " << error.what() << '\n';
        printUsage(std::cerr);
        return 2;
    }
    if (options.help) {
        printUsage(std::cout);
        return 0;
    }

    try {
        // 设备对象覆盖registry、工具和executor的完整生命周期。
        std::unique_ptr<VehicleDevice> device;
        if (options.device == "iio") {
            device = std::make_unique<LinuxVehicleDevice>(
                options.iio_root, options.iio_device_name
            );
        } else {
            auto mock = std::make_unique<MockVehicleDevice>();
            mock->setEnvironment(28.5F, 60.0F);
            device = std::move(mock);
        }

        ToolRegistry registry;
        registry.registerTool(std::make_unique<GetCabinEnvironmentTool>(*device));
        registry.registerTool(std::make_unique<CheckCabinTemperatureConditionTool>(*device));
        registry.registerTool(std::make_unique<SetAirConditionerIfTemperatureTool>(*device));
        registry.registerTool(std::make_unique<GetAirConditionerStateTool>(*device));
        registry.registerTool(std::make_unique<SetAirConditionerTool>(*device));

        RuleAgentPlanner planner;
        AgentExecutor executor(planner, registry);
        const std::string session_id = "console-session";

        std::cout << "Edge Vehicle Agent Demo\n"
                  << "[DEVICE] backend=" << options.device;
        if (options.device == "iio") {
            std::cout << " root=" << options.iio_root
                      << " name=" << options.iio_device_name;
        }
        std::cout << '\n'
                  << "[PLANNER] " << planner.name() << '\n'
                  << "温湿度按所选后端读取；空调/LED仍为进程内模拟状态。\n"
                  << "可输入：\n"
                  << "  车内温度是多少\n"
                  << "  车内湿度是多少\n"
                  << "  空调状态\n"
                  << "  打开空调\n"
                  << "  关闭空调\n"
                  << "  确认\n"
                  << "  取消\n"
                  << "  exit\n\n";

        while (true) {
            std::cout << "用户> ";
            std::string input;
            if (!std::getline(std::cin, input) || input == "exit" || input == "退出") {
                break;
            }

            const AgentResponse response = executor.run(session_id, input);
            if (!response.ok) {
                std::cout << "Agent error> " << response.error << '\n';
                continue;
            }

            std::cout << "Agent> " << response.answer << '\n';
            if (!response.executed_tool.empty()) {
                std::cout << "Tool> " << response.executed_tool << '\n'
                          << "Observation> " << response.observation.dump() << '\n';
            }
        }
        // 正常结束REPL不等于每一轮都成功；逐轮错误通过Agent error报告。
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Agent demo failed: " << error.what() << '\n';
        return 1;
    }
}
