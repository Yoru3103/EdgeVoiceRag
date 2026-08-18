#include <iostream>
#include <limits>
#include <string>

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

AgentToolResult check(
    CheckCabinTemperatureConditionTool& tool,
    const std::string& comparison_operator,
    double threshold_c
) {
    return tool.execute({
        {"operator", comparison_operator},
        {"threshold_c", threshold_c}
    });
}

void testDefinition() {
    MockVehicleDevice device;
    CheckCabinTemperatureConditionTool tool(device);

    expectTrue(
        tool.name()
            == "check_cabin_temperature_condition",
        "condition tool has expected name"
    );

    expectTrue(
        !tool.requiresConfirmation(),
        "condition check is read-only"
    );

    const nlohmann::json schema =
        tool.parametersSchema();

    expectTrue(
        schema.at("required").size() == 2,
        "schema requires two arguments"
    );

    expectTrue(
        !schema.at("additionalProperties")
            .get<bool>(),
        "schema rejects additional properties"
    );
}

void testGreaterThan() {
    MockVehicleDevice device;
    device.setEnvironment(29.5F, 55.0F);

    CheckCabinTemperatureConditionTool tool(device);

    const AgentToolResult matched =
        check(tool, "gt", 27.0);

    expectTrue(
        matched.ok,
        "greater-than comparison succeeds"
    );

    expectTrue(
        matched.data.at("matched").get<bool>(),
        "29.5 is greater than 27"
    );

    expectTrue(
        matched.data.at("temperature_c")
                .get<double>()
            == 29.5,
        "result contains measured temperature"
    );

    expectTrue(
        matched.data.at("threshold_c")
                .get<double>()
            == 27.0,
        "result contains threshold"
    );

    expectTrue(
        matched.data.at("operator")
                .get<std::string>()
            == "gt",
        "result contains comparison operator"
    );

    const AgentToolResult not_matched =
        check(tool, "gt", 30.0);

    expectTrue(
        not_matched.ok
            && !not_matched.data
                    .at("matched")
                    .get<bool>(),
        "29.5 is not greater than 30"
    );
}

void testEqualBoundaries() {
    MockVehicleDevice device;
    device.setEnvironment(27.0F, 50.0F);

    CheckCabinTemperatureConditionTool tool(device);

    const AgentToolResult gt =
        check(tool, "gt", 27.0);

    const AgentToolResult ge =
        check(tool, "ge", 27.0);

    const AgentToolResult lt =
        check(tool, "lt", 27.0);

    const AgentToolResult le =
        check(tool, "le", 27.0);

    expectTrue(
        gt.ok
            && !gt.data.at("matched").get<bool>(),
        "equal value does not match gt"
    );

    expectTrue(
        ge.ok
            && ge.data.at("matched").get<bool>(),
        "equal value matches ge"
    );

    expectTrue(
        lt.ok
            && !lt.data.at("matched").get<bool>(),
        "equal value does not match lt"
    );

    expectTrue(
        le.ok
            && le.data.at("matched").get<bool>(),
        "equal value matches le"
    );
}

void testLessThan() {
    MockVehicleDevice device;
    device.setEnvironment(18.5F, 45.0F);

    CheckCabinTemperatureConditionTool tool(device);

    const AgentToolResult lt =
        check(tool, "lt", 20.0);

    expectTrue(
        lt.ok
            && lt.data.at("matched").get<bool>(),
        "18.5 is less than 20"
    );

    const AgentToolResult le =
        check(tool, "le", 18.0);

    expectTrue(
        le.ok
            && !le.data.at("matched").get<bool>(),
        "18.5 is not less than or equal to 18"
    );
}

void testInvalidArguments() {
    MockVehicleDevice device;
    CheckCabinTemperatureConditionTool tool(device);

    expectTrue(
        !tool.execute(nlohmann::json::array()).ok,
        "array arguments are rejected"
    );

    expectTrue(
        !tool.execute({
            {"operator", "gt"}
        }).ok,
        "missing threshold is rejected"
    );

    expectTrue(
        !tool.execute({
            {"operator", "gt"},
            {"threshold_c", 27.0},
            {"unexpected", true}
        }).ok,
        "unknown argument is rejected"
    );

    expectTrue(
        !tool.execute({
            {"operator", "equal"},
            {"threshold_c", 27.0}
        }).ok,
        "unknown operator is rejected"
    );

    expectTrue(
        !tool.execute({
            {"operator", "gt"},
            {"threshold_c", "27"}
        }).ok,
        "string threshold is rejected"
    );

    expectTrue(
        !tool.execute({
            {"operator", "gt"},
            {
                "threshold_c",
                std::numeric_limits<
                    double
                >::quiet_NaN()
            }
        }).ok,
        "non-finite threshold is rejected"
    );

    expectTrue(
        !tool.execute({
            {"operator", "gt"},
            {"threshold_c", 200.0}
        }).ok,
        "out-of-range threshold is rejected"
    );
}

void testInvalidSensorValue() {
    MockVehicleDevice device;

    device.setEnvironment(
        std::numeric_limits<
            float
        >::quiet_NaN(),
        50.0F
    );

    CheckCabinTemperatureConditionTool tool(device);

    const AgentToolResult result =
        check(tool, "gt", 27.0);

    expectTrue(
        !result.ok,
        "invalid sensor temperature is rejected"
    );
}

void testDoesNotChangeDeviceState() {
    MockVehicleDevice device;
    device.setEnvironment(30.0F, 50.0F);

    CheckCabinTemperatureConditionTool tool(device);

    const AgentToolResult result =
        check(tool, "gt", 27.0);

    expectTrue(
        result.ok
            && result.data.at("matched").get<bool>(),
        "condition matches"
    );

    expectTrue(
        !device.airConditionerEnabled(),
        "condition check does not change device state"
    );
}

}  // namespace

int main() {
    testDefinition();
    testGreaterThan();
    testEqualBoundaries();
    testLessThan();
    testInvalidArguments();
    testInvalidSensorValue();
    testDoesNotChangeDeviceState();

    if (failed_count != 0) {
        std::cout
            << failed_count
            << " vehicle tool test(s) failed\n";

        return 1;
    }

    std::cout
        << "All vehicle tool tests passed\n";

    return 0;
}
