#pragma once

#include <atomic>
#include <filesystem>
#include <string>

#include "agent/vehicle_device.h"

namespace edge::agent {

/*
 * 真实Linux设备实现。
 *
 * 温湿度：
 *   通过Linux IIO sysfs读取edge_dht11驱动数据。
 *
 * 空调状态：
 *   当前阶段暂时保存在内存中。
 *   后续接入LED Class驱动后，再替换为真实LED控制。
 */
class LinuxVehicleDevice final : public VehicleDevice {
public:
    explicit LinuxVehicleDevice(
        std::filesystem::path iio_root = "/sys/bus/iio/devices",
        std::string iio_device_name = "edge_dht11"
    );

    CabinEnvironment readEnvironment() const override;

    bool setAirConditionerEnabled(bool enabled) override;

    bool airConditionerEnabled() const override;

private:
    std::filesystem::path findIioDevice() const;

    static std::string readTextFile(const std::filesystem::path& path);

    static long long readIntegerFile(const std::filesystem::path& path);

    std::filesystem::path iio_root_;
    std::string iio_device_name_;

    /*
     * 目前只是临时逻辑状态。
     * 后续替换成LED Class节点读写。
     */
    std::atomic_bool air_conditioner_enabled_{false};
};

}   // namespace edge::agent
