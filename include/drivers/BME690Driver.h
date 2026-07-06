#pragma once

#include <cstdint>
#include <vector>

#include "config.h"
#include "hardware/II2CBus.h"
#include "sensor_types.h"

namespace odor {

struct BME690Config {
    bool addressConfigured = true;
    uint8_t i2cAddress = config::Bme690I2cAddress;
    bool busAssignmentConfirmed = false;
    config::Bme690RuntimeSettings runtime{};
};

class BME690Driver {
public:
    BME690Driver(hardware::II2CBus& bus, const BME690Config& config);

    OperationResult begin();
    DriverStatus status() const;
    OperationResult readMeasurement(Bme690Measurement& measurement);

private:
    struct Calibration {
        uint16_t parT1 = 0;
        uint16_t parT2 = 0;
        int8_t parT3 = 0;
        int16_t parP1 = 0;
        uint16_t parP2 = 0;
        int8_t parP3 = 0;
        int8_t parP4 = 0;
        int16_t parP5 = 0;
        int16_t parP6 = 0;
        int8_t parP7 = 0;
        int8_t parP8 = 0;
        int16_t parP9 = 0;
        int8_t parP10 = 0;
        int8_t parP11 = 0;
        int16_t parH1 = 0;
        int8_t parH2 = 0;
        uint8_t parH3 = 0;
        int8_t parH4 = 0;
        int16_t parH5 = 0;
        uint8_t parH6 = 0;
        int8_t parG1 = 0;
        int16_t parG2 = 0;
        int8_t parG3 = 0;
        uint8_t resHeatRange = 0;
        int8_t resHeatVal = 0;
        int8_t rangeSwErr = 0;
    };

    OperationResult readRegister(uint8_t reg, uint8_t& value);
    OperationResult readRegisters(uint8_t reg, std::vector<uint8_t>& values);
    OperationResult writeRegister(uint8_t reg, uint8_t value);
    OperationResult loadCalibration();
    OperationResult configureForcedMode();
    OperationResult setSleepMode();
    OperationResult setForcedMode();
    float compensateTemperatureC(uint32_t adcTemperature) const;
    float compensatePressurePa(uint32_t adcPressure, float temperatureC) const;
    float compensateHumidityRh(uint16_t adcHumidity, float temperatureC) const;
    static float compensateGasResistanceOhm(uint16_t adcGasResistance, uint8_t gasRange);
    uint8_t heaterResistanceRegister(uint16_t heaterTemperatureC) const;
    static uint8_t heaterDurationRegister(uint16_t durationMs);

    hardware::II2CBus& bus_;
    BME690Config config_;
    DriverStatus status_;
    Calibration calibration_{};
    bool calibrationLoaded_ = false;
    int16_t ambientTemperatureC_ = 25;
};

}  // namespace odor
