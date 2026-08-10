#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

#include "agent/linux_vehicle_device.h"

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

}  // namespace

int main() {
    testReadsNamedIioDevice();
    testDeviceNumberIsNotHardcoded();
    testMissingDeviceProducesError();
    testTemporaryAirConditionerState();

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
