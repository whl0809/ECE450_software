#pragma once

#include <chrono>

#include "drivers/ADS114S06Driver.h"
#include "drivers/BME690Driver.h"
#include "drivers/MCP3421Driver.h"
#include "drivers/SGP41Driver.h"
#include "drivers/SHT45Driver.h"
#include "hardware/II2CBus.h"
#include "hardware/IGpioLine.h"
#include "hardware/ISPIDevice.h"
#include "sensor_types.h"

namespace odor {

struct SensorManagerRuntimeProfile {
    bool i2cBusAssignmentsConfirmed = false;
    bool adsSpiConfigured = false;
    bool adsStartConfigured = false;
    bool adsDrdyConfigured = false;
    bool enableTgsArray = config::EnableTgsArray;
    bool enableNh3Sensor = config::EnableNh3Sensor;
    bool enableH2sSensor = config::EnableH2sSensor;
    bool enableSgp41 = config::EnableSgp41;
    bool enableBme690 = config::EnableBme690;
    bool enableSht45 = config::EnableSht45;
    config::Ads114s06RuntimeSettings adsRuntime = config::Ads114s06Defaults;
};

class SensorManager {
public:
    SensorManager(hardware::II2CBus& i2c0,
                  hardware::II2CBus& i2c1,
                  hardware::ISPIDevice& adsSpi);
    SensorManager(hardware::II2CBus& i2c0,
                  hardware::II2CBus& i2c1,
                  hardware::ISPIDevice& adsSpi,
                  hardware::IGpioLine* adsDrdy,
                  const SensorManagerRuntimeProfile& profile);
    SensorManager(hardware::II2CBus& i2c0,
                  hardware::II2CBus& i2c1,
                  hardware::ISPIDevice& adsSpi,
                  hardware::IGpioLine* adsDrdy,
                  hardware::IGpioLine* adsStart,
                  const SensorManagerRuntimeProfile& profile);

    OperationResult begin();
    void update(std::chrono::steady_clock::time_point now);

    const SensorFrame& latestFrame() const;
    bool initialized() const;
    ErrorFlags errorFlags() const;

private:
    void refreshDriverStatus();
    void mergeStatus(const DriverStatus& status);

    hardware::II2CBus& i2c0_;
    hardware::II2CBus& i2c1_;
    hardware::ISPIDevice& adsSpi_;
    hardware::IGpioLine* adsDrdy_ = nullptr;
    hardware::IGpioLine* adsStart_ = nullptr;
    SensorManagerRuntimeProfile profile_{};

    ADS114S06Driver ads114s06_;
    MCP3421Driver nh3Mcp3421_;
    MCP3421Driver h2sMcp3421_;
    SHT45Driver sht45_;
    SGP41Driver sgp41_;
    BME690Driver bme690_;

    SensorFrame frame_;
    bool initialized_ = false;
    std::chrono::steady_clock::time_point lastUpdate_{};
};

}  // namespace odor
