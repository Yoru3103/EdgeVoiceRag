#include "agent/linux_vehicle_device.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace edge::agent {

namespace fs = std::filesystem;

LinuxVehicleDevice::LinuxVehicleDevice(
    fs::path iio_root,
    std::string iio_device_name
)
    : iio_root_(std::move(iio_root))
    , iio_device_name_(std::move(iio_device_name)) {
    if (iio_root_.empty()) {
        throw std::invalid_argument(
            "IIO root path must not be empty"
        );
    }

    if (iio_device_name_.empty()) {
        throw std::invalid_argument(
            "IIO device name must not be empty"
        );
    }
}

std::string LinuxVehicleDevice::readTextFile(const fs::path& path) {
    std::ifstream input(path);

    if (!input.is_open()) {
        throw std::runtime_error(
            "failed to open IIO file: " + path.string()
        );
    }

    std::string value;

    if (!(input >> value)) {
        throw std::runtime_error(
            "failed to read IIO file: " + path.string()
        );
    }

    return value;
}

long long LinuxVehicleDevice::readIntegerFile(const fs::path& path) {
    std::ifstream input(path);

    if (!input.is_open()) {
        throw std::runtime_error(
            "failed to open IIO value: " + path.string()
        );
    }

    long long value = 0;
    
    if (!(input >> value)) {
        throw std::runtime_error(
            "invalid integer in IIO file: " + path.string()
        );
    }

    /*
     * sysfs属性正常情况下只包含一个整数。
     * 如果整数后还有非空白内容，就认为数据格式异常。
     */
    std::string unexpected;

    if (input >> unexpected) {
        throw std::runtime_error(
            "unexpected data in IIO file: " + path.string()
        );
    }

    return value;
}

fs::path LinuxVehicleDevice::findIioDevice() const {
    std::error_code error;

    if (!fs::is_directory(iio_root_, error)) {
        throw std::runtime_error(
            "IIO root directory does not exist: "
            + iio_root_.string()
        );
    }

    fs::directory_iterator iterator(iio_root_, error);
    fs::directory_iterator end;

    if (error) {
        throw std::runtime_error(
            "failed to enumerate IIO devices: "
            + error.message()
        );
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
           throw std::runtime_error(
                "failed while enumerating IIO devices: "
                + error.message()
            ); 
        }

        const fs::directory_entry& entry = *iterator;

        if (!entry.is_directory(error)) {
            error.clear();
            continue;
        }

        const std::string directory_name = entry.path().filename().string();

        if (directory_name.rfind("iio:device", 0) != 0) {
            continue;
        }

        const fs::path name_path = entry.path() / "name";

        try {
            if (readTextFile(name_path) == iio_device_name_) {
                return entry.path();
            }
        } catch (const std::exception&) {
            /*
             * 某个IIO设备没有name或者暂时无法访问，
             * 继续检查其他设备。
             */
        }
    }

    throw std::runtime_error("IIO device not found: " + iio_device_name_);
}

CabinEnvironment LinuxVehicleDevice::readEnvironment() const {
    const fs::path device_path = findIioDevice();

    /*
     * IIO processed属性使用毫单位：
     *
     * 28500 -> 28.5摄氏度
     * 60000 -> 60.0%RH
     */
    const long long temperature_milli = readIntegerFile(device_path / "in_temp_input");
    const long long humidity_milli = readIntegerFile(device_path / "in_humidityrelative_input");

    /*
     * 驱动已经进行范围检查，这里再做一次应用层防御。
     */
    if (temperature_milli < -50000 || temperature_milli > 100000) {
        throw std::runtime_error(
            "IIO temperature is out of range: "
            + std::to_string(temperature_milli)
        );
    }

    if (humidity_milli < 0 || humidity_milli > 100000) {
        throw std::runtime_error(
            "IIO humidity is out of range: "
            + std::to_string(humidity_milli)
        );
    }

    CabinEnvironment environment;

    environment.temperature_c = static_cast<float>(temperature_milli) / 1000.0F;
    environment.humidity_percent = static_cast<float>(humidity_milli) / 1000.0F;

    return environment;
}

bool LinuxVehicleDevice::setAirConditionerEnabled(bool enabled) {
    /*
     * 当前阶段只保存逻辑状态。
     * 后续在这里写LED Class的brightness节点。
     */
    air_conditioner_enabled_.store(
        enabled,
        std::memory_order_relaxed
    );

    return true;
}

bool LinuxVehicleDevice::airConditionerEnabled() const {
    return air_conditioner_enabled_.load(std::memory_order_relaxed);
}

}   // namespace edge::agent
