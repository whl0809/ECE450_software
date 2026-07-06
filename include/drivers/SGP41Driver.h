#pragma once

#include <cstdint>
#include <chrono>
#include <cmath>

#include "config.h"
#include "hardware/II2CBus.h"
#include "sensor_types.h"

namespace odor {

struct SGP41Config {
    bool addressConfigured = false;
    uint8_t i2cAddress = 0;
    bool busAssignmentConfirmed = false;
    config::Sgp41RuntimeSettings runtime{};
};

class SGP41Driver {
public:
    SGP41Driver(hardware::II2CBus& bus, const SGP41Config& config);

    OperationResult begin();
    DriverStatus status() const;
    void setCompensation(float temperatureC,
                         float humidityRh,
                         std::chrono::steady_clock::time_point timestamp);
    OperationResult readRawSignals(Sgp41Measurement& measurement);

private:
    hardware::II2CBus& bus_;
    SGP41Config config_;
    DriverStatus status_;
    bool hasCompensation_ = false;
    float compensationTemperatureC_ = NAN;
    float compensationHumidityRh_ = NAN;
    std::chrono::steady_clock::time_point compensationTimestamp_{};
};

}  // namespace odor
