#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config.h"

namespace odor::app {

struct RuntimeConfig {
    struct I2cAdapter {
        std::string path;
    };

    struct GpioLineConfig {
        unsigned int lineOffset = 0;
        bool activeLow = false;
        bool configured = false;
    };

    I2cAdapter primaryI2c;
    I2cAdapter secondaryI2c;

    std::string adsSpiDevice;
    uint8_t adsSpiMode = 1;
    uint8_t adsBitsPerWord = 8;
    uint32_t adsMaxSpeedHz = 1000000;
    config::Ads114s06PgaGain adsPgaGain = config::Ads114s06PgaGain::X1;
    uint8_t adsDataRateCode = config::Ads114s06Defaults.dataRateCode;
    uint8_t adsFilterCode = config::Ads114s06Defaults.filterCode;
    bool adsVerifyRegisterReadback = true;
    bool adsWaitForDrdy = true;

    std::string gpioChipPath;
    std::string gpioChipExpectedLabel;
    GpioLineConfig adsStart;
    GpioLineConfig adsDrdy;
};

struct ConfigLoadResult {
    bool ok = false;
    RuntimeConfig config;
    std::vector<std::string> errors;
};

struct ValidationResult {
    bool ok = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

ConfigLoadResult loadRuntimeConfig(const std::string& path);
ValidationResult validateRuntimeConfigValues(const RuntimeConfig& config);

}  // namespace odor::app
