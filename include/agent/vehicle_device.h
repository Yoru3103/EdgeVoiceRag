#pragma once

namespace edge::agent {
    
struct CabinEnvironment {
    float temperature_c = 25.0F;
    float humidity_percent = 50.0F;
};

// Agent 依赖的是抽象设备，而不是真实 GPIO 或 DHT11。
class VehicleDevice {
public:
    virtual ~VehicleDevice() = default;

    virtual CabinEnvironment readEnvironment() const = 0;

    virtual bool setAirConditionerEnabled(bool enabled) = 0;

    virtual bool airConditionerEnabled() const = 0;
};

class MockVehicleDevice final : public VehicleDevice {
public:
    CabinEnvironment readEnvironment() const override;

    bool setAirConditionerEnabled(bool enabled) override;

    bool airConditionerEnabled() const override;

    void setEnvironment(
        float temperature_c,
        float humidity_percent
    );

    void setControlFailure(bool should_fail);

private:
    CabinEnvironment environment_;
    bool air_conditioner_enabled_ = false;
    bool control_should_fail_ = false;
};

}   // namespace edge::agent
