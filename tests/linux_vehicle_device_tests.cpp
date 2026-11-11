#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

#include "agent/linux_vehicle_device.h"
#include "agent/agent_executor.h"
#include "agent/rule_agent_planner.h"
#include "agent/tool_registry.h"
#include "agent/vehicle_tools.h"

namespace {

namespace fs = std::filesystem;

using edge::agent::CabinEnvironment;
using edge::agent::LinuxVehicleDevice;

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

void expectNear(
    float actual,
    float expected,
    float tolerance,
    const std::string& name
) {
    expectTrue(
        std::fabs(actual - expected) <= tolerance,
        name
    );
}

class TemporaryIioTree {
public:
    TemporaryIioTree() {
        const auto unique_value =
            std::chrono::steady_clock::now()
                .time_since_epoch()
                .count();

        root_ =
            fs::temp_directory_path()
            / (
                "edge_voice_iio_test_"
                + std::to_string(unique_value)
            );

        fs::create_directories(root_);
    }

    ~TemporaryIioTree() {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    const fs::path& root() const {
        return root_;
    }

    fs::path createDevice(
        const std::string& directory_name,
        const std::string& device_name,
        const std::string& temperature,
        const std::string& humidity
    ) {
        const fs::path device =
            root_ / directory_name;

        fs::create_directories(device);

        writeFile(device / "name", device_name);
        writeFile(
            device / "in_temp_input",
            temperature
        );
        writeFile(
            device / "in_humidityrelative_input",
            humidity
        );

        return device;
    }

private:
    static void writeFile(
        const fs::path& path,
        const std::string& value
    ) {
        std::ofstream output(path);

        if (!output.is_open()) {
            throw std::runtime_error(
                "failed to create test file: "
                + path.string()
            );
        }

        output << value << '\n';
    }

    fs::path root_;
};

void testReadsNamedIioDevice() {
    TemporaryIioTree tree;

    tree.createDevice(
        "iio:device0",
        "rk-adc",
        "1000",
        "2000"
    );

    tree.createDevice(
        "iio:device3",
        "edge_dht11",
        "28500",
        "60500"
    );

    LinuxVehicleDevice device(
        tree.root(),
        "edge_dht11"
    );

    const CabinEnvironment environment =
        device.readEnvironment();

    expectNear(
        environment.temperature_c,
        28.5F,
        0.001F,
        "temperature converts from milli Celsius"
    );

    expectNear(
        environment.humidity_percent,
        60.5F,
        0.001F,
        "humidity converts from milli percent"
    );
}

void testDeviceNumberIsNotHardcoded() {
    TemporaryIioTree tree;

    tree.createDevice(
        "iio:device8",
        "edge_dht11",
        "23000",
        "45000"
    );

    LinuxVehicleDevice device(
        tree.root(),
        "edge_dht11"
    );

    const CabinEnvironment environment =
        device.readEnvironment();

    expectNear(
        environment.temperature_c,
        23.0F,
        0.001F,
        "finds dynamically numbered IIO device"
    );
}

void testMissingDeviceProducesError() {
    TemporaryIioTree tree;

    LinuxVehicleDevice device(
        tree.root(),
        "edge_dht11"
    );

    bool error_thrown = false;

    try {
        static_cast<void>(
            device.readEnvironment()
        );
    } catch (const std::runtime_error&) {
        error_thrown = true;
    }

    expectTrue(
        error_thrown,
        "missing IIO device produces an error"
    );
}

void testTemporaryAirConditionerState() {
    TemporaryIioTree tree;

    LinuxVehicleDevice device(
        tree.root(),
        "edge_dht11"
    );

    expectTrue(
        !device.airConditionerEnabled(),
        "air conditioner starts disabled"
    );

    expectTrue(
        device.setAirConditionerEnabled(true),
        "temporary air conditioner control succeeds"
    );

    expectTrue(
        device.airConditionerEnabled(),
        "temporary air conditioner state is stored"
    );
}


struct RuleIioFixture {
    TemporaryIioTree tree;
    edge::agent::LinuxVehicleDevice device{tree.root(), "edge_dht11"};
    edge::agent::ToolRegistry registry;
    edge::agent::RuleAgentPlanner planner;
    std::unique_ptr<edge::agent::AgentExecutor> executor;

    RuleIioFixture() {
        tree.createDevice("iio:device0", "rk-adc", "1000", "2000");
        environment("31200", "47000");
        registry.registerTool(std::make_unique<edge::agent::GetCabinEnvironmentTool>(device));
        registry.registerTool(std::make_unique<edge::agent::CheckCabinTemperatureConditionTool>(device));
        registry.registerTool(std::make_unique<edge::agent::SetAirConditionerIfTemperatureTool>(device));
        registry.registerTool(std::make_unique<edge::agent::GetAirConditionerStateTool>(device));
        registry.registerTool(std::make_unique<edge::agent::SetAirConditionerTool>(device));
        executor = std::make_unique<edge::agent::AgentExecutor>(planner, registry);
    }

    void environment(const std::string& temperature, const std::string& humidity) {
        tree.createDevice("iio:device7", "edge_dht11", temperature, humidity);
    }
};

void testRuleAgentReadsIioAndRefreshesEnvironment() {
    RuleIioFixture fixture;
    auto response = fixture.executor->run("iio-session", "车内温度是多少");
    expectTrue(
        response.ok && response.state == edge::agent::AgentResponseState::Completed
            && response.executed_tool == "get_cabin_environment",
        "Rule Agent completes an IIO environment tool call"
    );
    expectNear(
        response.observation.value("temperature_c", -999.0F), 31.2F, 0.001F,
        "Rule Agent receives converted IIO temperature, not Mock default"
    );
    expectNear(
        response.observation.value("humidity_percent", -999.0F), 47.0F, 0.001F,
        "Rule Agent receives converted IIO humidity"
    );

    fixture.environment("29900", "48500");
    response = fixture.executor->run("iio-session", "现在多少度？");
    expectTrue(response.ok, "same Agent can query IIO on a later turn");
    expectNear(
        response.observation.value("temperature_c", -999.0F), 29.9F, 0.001F,
        "later query observes updated IIO fixture temperature"
    );
    expectNear(
        response.observation.value("humidity_percent", -999.0F), 48.5F, 0.001F,
        "later query observes updated IIO fixture humidity"
    );
}

void testRuleIioWorkflowRequiresConfirmation() {
    RuleIioFixture fixture;
    auto response = fixture.executor->run("iio-session", "打开空调");
    expectTrue(
        response.ok
            && response.state == edge::agent::AgentResponseState::WaitingForConfirmation
            && !fixture.device.airConditionerEnabled(),
        "IIO-backed Agent does not change logical AC state before confirmation"
    );
    response = fixture.executor->run("iio-session", "取消");
    expectTrue(
        response.ok && !fixture.executor->hasPendingAction("iio-session")
            && !fixture.device.airConditionerEnabled(),
        "cancellation clears pending action without changing state"
    );
    fixture.executor->run("iio-session", "打开空调");
    response = fixture.executor->run("iio-session", "确认");
    expectTrue(
        response.ok && fixture.device.airConditionerEnabled()
            && !fixture.executor->hasPendingAction("iio-session"),
        "confirmation executes the pending in-memory AC action"
    );
    response = fixture.executor->run("iio-session", "空调状态");
    expectTrue(
        response.ok && response.executed_tool == "get_air_conditioner_state"
            && response.observation.value("enabled", false),
        "subsequent query observes the confirmed logical state"
    );
    fixture.executor->run("iio-session", "关闭空调");
    response = fixture.executor->run("iio-session", "确认");
    expectTrue(
        response.ok && !fixture.device.airConditionerEnabled(),
        "confirmed close restores logical AC state"
    );
}

void testRuleAgentReportsIioErrorsAndRecovers() {
    RuleIioFixture fixture;
    fixture.environment("not-an-integer", "47000");
    auto response = fixture.executor->run("iio-session", "车内温度是多少");
    expectTrue(
        !response.ok && response.state == edge::agent::AgentResponseState::Failed
            && response.error.find("IIO") != std::string::npos
            && !fixture.device.airConditionerEnabled(),
        "IIO data error is reported through Agent without a fabricated reading"
    );
    fixture.environment("30500", "49000");
    response = fixture.executor->run("iio-session", "车内温度是多少");
    expectTrue(response.ok, "Agent recovers after IIO data becomes valid");
    expectNear(
        response.observation.value("temperature_c", -999.0F), 30.5F, 0.001F,
        "recovery returns the new IIO value"
    );

    fs::remove(fixture.tree.root() / "iio:device7" / "name");
    response = fixture.executor->run("iio-session", "车内湿度是多少");
    expectTrue(
        !response.ok && response.error.find("IIO") != std::string::npos,
        "missing IIO device propagates as Agent failure"
    );
}

}  // namespace

int main() {
    testReadsNamedIioDevice();
    testDeviceNumberIsNotHardcoded();
    testMissingDeviceProducesError();
    testTemporaryAirConditionerState();
    testRuleAgentReadsIioAndRefreshesEnvironment();
    testRuleIioWorkflowRequiresConfirmation();
    testRuleAgentReportsIioErrorsAndRecovers();

    if (failed_count != 0) {
        std::cerr
            << failed_count
            << " Linux vehicle device test(s) failed\n";

        return 1;
    }

    std::cout
        << "All Linux vehicle device tests passed\n";

    return 0;
}
