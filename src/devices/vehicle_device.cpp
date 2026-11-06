#include "agent/vehicle_device.h"

namespace edge::agent {

CabinEnvironment MockVehicleDevice::readEnvironment() const {
    return environment_;
}

bool MockVehicleDevice::setAirConditionerEnabled(bool enabled) {
    if (control_should_fail_) {
        return false;
    }

    air_conditioner_enabled_ = enabled;
    return true;
}

bool MockVehicleDevice::airConditionerEnabled() const {
    return air_conditioner_enabled_;
}

void MockVehicleDevice::setEnvironment(
    float temperature_c,
    float humidity_percent
) {
    environment_.temperature_c = temperature_c;
    environment_.humidity_percent = humidity_percent;
}

void MockVehicleDevice::setControlFailure(
    bool should_fail
) {
    control_should_fail_ = should_fail;
}

}
