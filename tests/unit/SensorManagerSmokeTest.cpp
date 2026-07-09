#include <chrono>
#include <cstdint>
#include <fstream>

#include "app/RuntimeConfig.h"
#include "error_flags.h"
#include "hardware/mock/MockI2CBus.h"
#include "hardware/mock/MockSPIDevice.h"
#include "services/SensorManager.h"

namespace {

int expect(bool condition)
{
    return condition ? 0 : 1;
}

}  // namespace

int main()
{
    odor::hardware::mock::MockI2CBus primaryI2c(false, "test-primary-i2c");
    odor::hardware::mock::MockI2CBus secondaryI2c(false, "test-secondary-i2c");
    odor::hardware::mock::MockSPIDevice adsSpi(false, "test-spi");

    odor::SensorManager manager(primaryI2c, secondaryI2c, adsSpi);
    const odor::OperationResult beginResult = manager.begin();

    int failures = 0;
    failures += expect(beginResult.ok);
    failures += expect(manager.initialized());
    failures += expect(odor::hasError(manager.errorFlags(), odor::ErrorFlag::DeviceNotConfigured));

    manager.update(std::chrono::steady_clock::now());
    const odor::SensorFrame& frame = manager.latestFrame();
    failures += expect(frame.validFlags == 0U);
    failures += expect(odor::hasError(frame.errorFlags, odor::ErrorFlag::DeviceNotConfigured));

    odor::app::RuntimeConfig config;
    config.primaryI2c.path = "/dev/i2c-1";
    config.adsSpiDevice = "/dev/spidev0.0";
    config.adsSpiMode = 1;
    config.adsBitsPerWord = 8;
    config.adsMaxSpeedHz = 1000000;
    config.gpioChipPath = "/dev/gpiochip4";
    config.gpioChipExpectedLabel = "pinctrl-rp1";
    config.adsStart = {17, false, true};
    config.adsDrdy = {27, true, true};
    failures += expect(odor::app::validateRuntimeConfigValues(config).ok);

    config.adsSpiMode = 0;
    failures += expect(!odor::app::validateRuntimeConfigValues(config).ok);

    return failures;
}
