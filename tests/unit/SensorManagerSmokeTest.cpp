#include <chrono>
#include <cstdint>
#include <cstdio>
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

    const char* tgsOnlyPath = "tgs-only-test.toml";
    {
        std::ofstream out(tgsOnlyPath);
        out << "[sensors.sht45]\n"
            << "enabled = false\n"
            << "[sensors.sgp41]\n"
            << "enabled = false\n"
            << "[sensors.bme690]\n"
            << "enabled = false\n"
            << "[sensors.nh3_mcp3421]\n"
            << "enabled = false\n"
            << "[sensors.h2s_mcp3421]\n"
            << "enabled = false\n"
            << "[ads114s06]\n"
            << "enabled = true\n"
            << "[ads114s06.spi]\n"
            << "device = \"/dev/spidev0.0\"\n"
            << "mode = 1\n"
            << "bits_per_word = 8\n"
            << "max_speed_hz = 1000000\n"
            << "[ads114s06.gpio]\n"
            << "gpiochip = \"/dev/gpiochip4\"\n"
            << "expected_label = \"pinctrl-rp1\"\n"
            << "start_line_offset = 17\n"
            << "start_active_low = false\n"
            << "drdy_line_offset = 27\n"
            << "drdy_active_low = true\n";
    }
    const odor::app::ConfigLoadResult tgsOnly = odor::app::loadRuntimeConfig(tgsOnlyPath);
    failures += expect(tgsOnly.ok);
    failures += expect(!tgsOnly.config.enableSht45);
    failures += expect(!tgsOnly.config.enableSgp41);
    failures += expect(!tgsOnly.config.enableBme690);
    failures += expect(!tgsOnly.config.enableNh3Mcp3421);
    failures += expect(!tgsOnly.config.enableH2sMcp3421);
    failures += expect(tgsOnly.config.enableAds114s06);
    failures += expect(odor::app::validateRuntimeConfigValues(tgsOnly.config).ok);
    std::remove(tgsOnlyPath);

    return failures;
}
