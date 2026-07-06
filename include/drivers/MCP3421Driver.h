#pragma once

#include <cstdint>

#include "config.h"
#include "hardware/II2CBus.h"
#include "sensor_types.h"

namespace odor {

struct MCP3421Config {
    bool addressConfigured = false;
    uint8_t i2cAddress = 0;
    bool busAssignmentConfirmed = false;
    const char* label = nullptr;
    config::ElectrochemicalFrontEndConfig frontEnd{};
    config::Mcp3421RuntimeSettings runtime{};
};

class MCP3421Driver {
public:
    MCP3421Driver(hardware::II2CBus& bus, const MCP3421Config& config);

    OperationResult begin();
    DriverStatus status() const;
    OperationResult readElectrochemical(ElectrochemicalMeasurement& measurement);

private:
    hardware::II2CBus& bus_;
    MCP3421Config config_;
    DriverStatus status_;
};

}  // namespace odor
