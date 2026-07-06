#pragma once

#include "hardware/IGpioLine.h"
#include "hardware/ISPIDevice.h"
#include "config.h"
#include "sensor_types.h"

#include <cstdint>
#include <vector>

namespace odor {

struct ADS114S06Config {
    bool spiDeviceConfigured = false;
    bool drdyConfigured = false;
    bool startConfigured = false;
    bool chipSelectPermanentlyAsserted = true;
    bool resetControlledByRaspberryPi = false;
    float referenceVoltageV = 4.096F;
    config::Ads114s06RuntimeSettings runtime{};
};

class ADS114S06Driver {
public:
    ADS114S06Driver(hardware::ISPIDevice& spiDevice, const ADS114S06Config& config);
    ADS114S06Driver(hardware::ISPIDevice& spiDevice,
                    hardware::IGpioLine* drdyLine,
                    const ADS114S06Config& config);

    OperationResult begin();
    DriverStatus status() const;
    OperationResult readTgsArray(TgsArrayMeasurement& measurement);

private:
    OperationResult writeRegister(uint8_t reg, uint8_t value);
    OperationResult readRegister(uint8_t reg, uint8_t& value);
    OperationResult writeRegisterChecked(uint8_t reg, uint8_t value);
    OperationResult configureRegisters();
    OperationResult selectChannel(uint8_t ain);
    OperationResult startConversion();
    OperationResult waitForReady();
    OperationResult readSample(int32_t& rawCode);
    uint8_t pgaRegisterValue() const;
    uint8_t dataRateRegisterValue() const;

    hardware::ISPIDevice& spiDevice_;
    hardware::IGpioLine* drdyLine_ = nullptr;
    ADS114S06Config config_;
    DriverStatus status_;
};

}  // namespace odor
