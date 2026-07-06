#include "services/SensorManager.h"

#include "config.h"

namespace odor {

namespace {

ADS114S06Config makeAdsConfig()
{
    return {
        false,
        false,
        config::Ads114s06StartExposedOnConnector,
        config::Ads114s06ChipSelectPermanentlyAsserted,
        config::Ads114s06ResetControlledByRaspberryPi,
        config::Ads114s06ReferenceVoltageV,
        config::Ads114s06Defaults,
    };
}

MCP3421Config makeMcpConfig(const char* label,
                            uint8_t i2cAddress,
                            config::ElectrochemicalFrontEndConfig frontEnd)
{
    return {
        true,
        i2cAddress,
        false,
        label,
        frontEnd,
        config::Mcp3421Defaults,
    };
}

SHT45Config makeSht45Config()
{
    return {
        true,
        config::Sht45I2cAddress,
        false,
        config::Sht45Defaults,
    };
}

SGP41Config makeSgp41Config()
{
    return {
        true,
        config::Sgp41I2cAddress,
        false,
        config::Sgp41Defaults,
    };
}

BME690Config makeBme690Config()
{
    return {
        true,
        config::Bme690I2cAddress,
        false,
        config::Bme690Defaults,
    };
}

}  // namespace

SensorManager::SensorManager(hardware::II2CBus& i2c0,
                             hardware::II2CBus& i2c1,
                             hardware::ISPIDevice& adsSpi)
    : i2c0_(i2c0),
      i2c1_(i2c1),
      adsSpi_(adsSpi),
      ads114s06_(adsSpi_, makeAdsConfig()),
      nh3Mcp3421_(i2c0_, makeMcpConfig("NH3", config::Nh3Mcp3421I2cAddress, config::Nh3FrontEnd)),
      h2sMcp3421_(i2c1_, makeMcpConfig("H2S", config::H2sMcp3421I2cAddress, config::H2sFrontEnd)),
      sht45_(i2c0_, makeSht45Config()),
      sgp41_(i2c0_, makeSgp41Config()),
      bme690_(i2c0_, makeBme690Config())
{
}

OperationResult SensorManager::begin()
{
    frame_ = SensorFrame{};
    frame_.systemState = SystemState::ConfigurationMissing;

    if constexpr (config::EnableTgsArray) {
        (void)ads114s06_.begin();
    }
    if constexpr (config::EnableNh3Sensor) {
        (void)nh3Mcp3421_.begin();
    }
    if constexpr (config::EnableH2sSensor) {
        (void)h2sMcp3421_.begin();
    }
    if constexpr (config::EnableSht45) {
        (void)sht45_.begin();
    }
    if constexpr (config::EnableSgp41) {
        (void)sgp41_.begin();
    }
    if constexpr (config::EnableBme690) {
        (void)bme690_.begin();
    }

    initialized_ = true;
    refreshDriverStatus();
    return {true, frame_.errorFlags};
}

void SensorManager::update(std::chrono::steady_clock::time_point now)
{
    if (!initialized_) {
        return;
    }

    if (lastUpdate_ != std::chrono::steady_clock::time_point{} &&
        (now - lastUpdate_) < config::SensorManagerUpdateInterval) {
        return;
    }

    lastUpdate_ = now;
    frame_.monotonicTimestamp = now;
    frame_.wallTimestamp = std::chrono::system_clock::now();
    refreshDriverStatus();
    frame_.systemState = frame_.errorFlags == 0 ? SystemState::Running : SystemState::Degraded;
}

const SensorFrame& SensorManager::latestFrame() const
{
    return frame_;
}

bool SensorManager::initialized() const
{
    return initialized_;
}

ErrorFlags SensorManager::errorFlags() const
{
    return frame_.errorFlags;
}

void SensorManager::refreshDriverStatus()
{
    frame_.validFlags = 0;
    frame_.errorFlags = 0;

    if constexpr (config::EnableTgsArray) {
        const DriverStatus status = ads114s06_.status();
        frame_.tgs.valid = status.detected && status.errorFlags == 0;
        frame_.tgs.errorFlags = status.errorFlags;
        if (frame_.tgs.valid) {
            setValidFlag(frame_.validFlags, SensorGroup::TgsArray);
        }
        mergeStatus(status);
    }

    if constexpr (config::EnableNh3Sensor) {
        const DriverStatus status = nh3Mcp3421_.status();
        frame_.nh3.valid = status.detected && status.errorFlags == 0;
        frame_.nh3.errorFlags = status.errorFlags;
        if (frame_.nh3.valid) {
            setValidFlag(frame_.validFlags, SensorGroup::Nh3);
        }
        mergeStatus(status);
    }

    if constexpr (config::EnableH2sSensor) {
        const DriverStatus status = h2sMcp3421_.status();
        frame_.h2s.valid = status.detected && status.errorFlags == 0;
        frame_.h2s.errorFlags = status.errorFlags;
        if (frame_.h2s.valid) {
            setValidFlag(frame_.validFlags, SensorGroup::H2s);
        }
        mergeStatus(status);
    }

    if constexpr (config::EnableSht45) {
        const DriverStatus status = sht45_.status();
        frame_.sht45.valid = status.detected && status.errorFlags == 0;
        frame_.sht45.errorFlags = status.errorFlags;
        if (frame_.sht45.valid) {
            setValidFlag(frame_.validFlags, SensorGroup::Sht45);
        }
        mergeStatus(status);
    }

    if constexpr (config::EnableSgp41) {
        const DriverStatus status = sgp41_.status();
        frame_.sgp41.rawValid = status.detected && status.errorFlags == 0;
        frame_.sgp41.indexValid = false;
        frame_.sgp41.errorFlags = status.errorFlags;
        if (frame_.sgp41.rawValid) {
            setValidFlag(frame_.validFlags, SensorGroup::Sgp41);
        }
        mergeStatus(status);
    }

    if constexpr (config::EnableBme690) {
        const DriverStatus status = bme690_.status();
        frame_.bme690.valid = status.detected && status.errorFlags == 0;
        frame_.bme690.errorFlags = status.errorFlags;
        if (frame_.bme690.valid) {
            setValidFlag(frame_.validFlags, SensorGroup::Bme690);
        }
        mergeStatus(status);
    }
}

void SensorManager::mergeStatus(const DriverStatus& status)
{
    frame_.errorFlags |= status.errorFlags;
}

}  // namespace odor
