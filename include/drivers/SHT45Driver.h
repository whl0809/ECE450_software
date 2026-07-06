#pragma once

#include <cstdint>

#include "config.h"
#include "hardware/II2CBus.h"
#include "sensor_types.h"

namespace odor {

struct SHT45Config {
    bool addressConfigured = false;
    uint8_t i2cAddress = 0;
    bool busAssignmentConfirmed = false;
    config::Sht45RuntimeSettings runtime{};
};

class SHT45Driver {
public:
    SHT45Driver(hardware::II2CBus& bus, const SHT45Config& config);

    OperationResult begin();
    DriverStatus status() const;
    OperationResult readMeasurement(Sht45Measurement& measurement);

private:
    hardware::II2CBus& bus_;
    SHT45Config config_;
    DriverStatus status_;
};

}  // namespace odor
