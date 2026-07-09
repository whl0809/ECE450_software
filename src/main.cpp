#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "app/RuntimeConfig.h"
#include "config.h"
#include "drivers/ADS114S06Driver.h"
#include "drivers/BME690Driver.h"
#include "drivers/MCP3421Driver.h"
#include "drivers/SGP41Driver.h"
#include "drivers/SHT45Driver.h"
#include "hardware/mock/MockGpioLine.h"
#include "hardware/mock/MockI2CBus.h"
#include "hardware/mock/MockSPIDevice.h"
#include "services/SensorManager.h"

#if defined(ODOR_HAS_LINUX_HARDWARE)
#include "app/LinuxHardwareValidation.h"
#include "hardware/linux/LinuxGpioLine.h"
#include "hardware/linux/LinuxI2CBus.h"
#include "hardware/linux/LinuxSPIDevice.h"
#endif

namespace {

std::atomic_bool shutdownRequested{false};

struct CommandLine {
    std::string configPath = "config/odor-sensing.rpi5.toml";
    bool diagnostic = false;
    bool validateOnly = false;
    bool forceMock = false;
};

void handleSignal(int)
{
    shutdownRequested.store(true);
}

const char* initializationStatusText(const odor::OperationResult& result)
{
    return (result.ok && result.errorFlags == 0U) ? "OK" : "DEGRADED";
}

CommandLine parseCommandLine(int argc, char** argv)
{
    CommandLine options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--diagnostic") {
            options.diagnostic = true;
        } else if (arg == "--validate-only") {
            options.validateOnly = true;
        } else if (arg == "--mock") {
            options.forceMock = true;
        } else if (arg == "--config" && (i + 1) < argc) {
            options.configPath = argv[++i];
        }
    }
    return options;
}

void printValidation(const odor::app::ValidationResult& result)
{
    for (const std::string& warning : result.warnings) {
        std::cerr << "warning: " << warning << '\n';
    }
    for (const std::string& error : result.errors) {
        std::cerr << "error: " << error << '\n';
    }
}

odor::config::Ads114s06RuntimeSettings adsRuntimeFrom(const odor::app::RuntimeConfig& config)
{
    odor::config::Ads114s06RuntimeSettings runtime = odor::config::Ads114s06Defaults;
    runtime.spiMode = config.adsSpiMode;
    runtime.bitsPerWord = config.adsBitsPerWord;
    runtime.maxSpeedHz = config.adsMaxSpeedHz;
    runtime.pgaGain = config.adsPgaGain;
    runtime.dataRateCode = config.adsDataRateCode;
    runtime.filterCode = config.adsFilterCode;
    runtime.verifyRegisterReadback = config.adsVerifyRegisterReadback;
    runtime.waitForDrdyWhenConfigured = config.adsWaitForDrdy;
    return runtime;
}

void printDiagnosticResult(const std::string& name, const odor::OperationResult& result)
{
    std::cout << "diagnostic," << name << ",ok=" << (result.ok ? "true" : "false")
              << ",error_flags=" << result.errorFlags << '\n';
}

int runMockApplication()
{
    odor::hardware::mock::MockI2CBus primaryI2c(false, "unconfigured-primary-i2c");
    odor::hardware::mock::MockI2CBus secondaryI2c(false, "unconfigured-secondary-i2c");
    odor::hardware::mock::MockSPIDevice adsSpi(false, "unconfigured-ads114s06-spi");
    odor::SensorManager sensorManager(primaryI2c, secondaryI2c, adsSpi);

    const odor::OperationResult beginResult = sensorManager.begin();
    std::cout << "Hardware access: disabled; using mock unconfigured interfaces\n";
    std::cout << "SensorManager initialization: " << initializationStatusText(beginResult) << '\n';
    std::cout << "Sensor interfaces are not enabled\n";

    const auto start = std::chrono::steady_clock::now();
    auto nextHeartbeat = start + odor::config::HeartbeatInterval;
    while (!shutdownRequested.load()) {
        const auto now = std::chrono::steady_clock::now();
        sensorManager.update(now);
        if (now >= nextHeartbeat) {
            const auto uptimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            std::cout << "heartbeat,uptime_ms=" << uptimeMs << '\n';
            nextHeartbeat += odor::config::HeartbeatInterval;
        }
        std::this_thread::sleep_until(std::min(nextHeartbeat, now + std::chrono::milliseconds(50)));
    }
    return 0;
}

#if defined(ODOR_HAS_LINUX_HARDWARE)
int runHardwareDiagnostic(odor::hardware::II2CBus& primaryI2c,
                          odor::hardware::II2CBus& secondaryI2c,
                          odor::hardware::ISPIDevice& adsSpi,
                          odor::hardware::IGpioLine& adsDrdy,
                          const odor::app::RuntimeConfig& config)
{
    const odor::config::Ads114s06RuntimeSettings adsRuntime = adsRuntimeFrom(config);

    odor::SHT45Driver sht45(primaryI2c, {true, odor::config::Sht45I2cAddress, true, odor::config::Sht45Defaults});
    odor::SGP41Driver sgp41(primaryI2c, {true, odor::config::Sgp41I2cAddress, true, odor::config::Sgp41Defaults});
    odor::BME690Driver bme690(primaryI2c, {true, odor::config::Bme690I2cAddress, true, odor::config::Bme690Defaults});
    odor::MCP3421Driver nh3(primaryI2c,
                            {true,
                             odor::config::Nh3Mcp3421I2cAddress,
                             true,
                             "NH3",
                             odor::config::Nh3FrontEnd,
                             odor::config::Mcp3421Defaults});
    odor::MCP3421Driver h2s(secondaryI2c,
                            {true,
                             odor::config::H2sMcp3421I2cAddress,
                             true,
                             "H2S",
                             odor::config::H2sFrontEnd,
                             odor::config::Mcp3421Defaults});
    odor::ADS114S06Driver ads(adsSpi,
                              &adsDrdy,
                              {true,
                               config.adsDrdy.configured,
                               config.adsStart.configured,
                               odor::config::Ads114s06ChipSelectPermanentlyAsserted,
                               odor::config::Ads114s06ResetControlledByRaspberryPi,
                               odor::config::Ads114s06ReferenceVoltageV,
                               adsRuntime});

    int failures = 0;
    auto record = [&](const std::string& name, const odor::OperationResult& result) {
        printDiagnosticResult(name, result);
        if (!result.ok) {
            ++failures;
        }
    };

    record("sht45_begin", sht45.begin());
    odor::Sht45Measurement sht45Measurement;
    record("sht45_read", sht45.readMeasurement(sht45Measurement));

    record("sgp41_begin", sgp41.begin());
    if (sht45Measurement.valid) {
        sgp41.setCompensation(sht45Measurement.temperatureC,
                              sht45Measurement.humidityRh,
                              sht45Measurement.monotonicTimestamp);
    }
    odor::Sgp41Measurement sgp41Measurement;
    record("sgp41_read_raw", sgp41.readRawSignals(sgp41Measurement));

    record("bme690_begin", bme690.begin());
    odor::Bme690Measurement bme690Measurement;
    record("bme690_read", bme690.readMeasurement(bme690Measurement));

    record("nh3_mcp3421_begin", nh3.begin());
    odor::ElectrochemicalMeasurement nh3Measurement;
    record("nh3_mcp3421_read", nh3.readElectrochemical(nh3Measurement));

    record("h2s_mcp3421_begin", h2s.begin());
    odor::ElectrochemicalMeasurement h2sMeasurement;
    record("h2s_mcp3421_read", h2s.readElectrochemical(h2sMeasurement));

    record("ads114s06_begin", ads.begin());
    odor::TgsArrayMeasurement tgsMeasurement;
    record("ads114s06_read_tgs_array", ads.readTgsArray(tgsMeasurement));

    return failures == 0 ? 0 : 2;
}

int runLinuxApplication(const odor::app::RuntimeConfig& config, bool diagnostic)
{
    odor::hardware::linux::LinuxI2CBus primaryI2c(config.primaryI2c.path);
    const odor::hardware::HardwareResult primaryOpen = primaryI2c.open();
    if (!primaryOpen.ok) {
        std::cerr << "error: " << primaryOpen.message << '\n';
        return 1;
    }

    std::unique_ptr<odor::hardware::linux::LinuxI2CBus> secondaryI2c;
    odor::hardware::II2CBus* h2sBus = &primaryI2c;
    if (!config.secondaryI2c.path.empty() && config.secondaryI2c.path != config.primaryI2c.path) {
        secondaryI2c = std::make_unique<odor::hardware::linux::LinuxI2CBus>(config.secondaryI2c.path);
        const odor::hardware::HardwareResult secondaryOpen = secondaryI2c->open();
        if (!secondaryOpen.ok) {
            std::cerr << "error: " << secondaryOpen.message << '\n';
            return 1;
        }
        h2sBus = secondaryI2c.get();
    }

    odor::hardware::linux::LinuxSPIDevice adsSpi({
        config.adsSpiDevice,
        config.adsSpiMode,
        config.adsBitsPerWord,
        config.adsMaxSpeedHz,
    });
    const odor::hardware::HardwareResult spiOpen = adsSpi.open();
    if (!spiOpen.ok) {
        std::cerr << "error: " << spiOpen.message << '\n';
        return 1;
    }

    odor::hardware::linux::LinuxGpioLine adsStart({
        config.gpioChipPath,
        config.adsStart.lineOffset,
        "odor-sensing-ads-start",
        odor::hardware::linux::GpioDirection::Output,
        odor::hardware::GpioEdge::None,
        config.adsStart.activeLow,
        false,
    });
    const odor::hardware::HardwareResult startRequest = adsStart.request();
    if (!startRequest.ok) {
        std::cerr << "error: " << startRequest.message << '\n';
        return 1;
    }

    odor::hardware::linux::LinuxGpioLine adsDrdy({
        config.gpioChipPath,
        config.adsDrdy.lineOffset,
        "odor-sensing-ads-drdy",
        odor::hardware::linux::GpioDirection::Input,
        odor::hardware::GpioEdge::Falling,
        config.adsDrdy.activeLow,
        false,
    });
    const odor::hardware::HardwareResult drdyRequest = adsDrdy.request();
    if (!drdyRequest.ok) {
        std::cerr << "error: " << drdyRequest.message << '\n';
        return 1;
    }

    if (diagnostic) {
        return runHardwareDiagnostic(primaryI2c, *h2sBus, adsSpi, adsDrdy, config);
    }

    odor::SensorManagerRuntimeProfile profile;
    profile.i2cBusAssignmentsConfirmed = true;
    profile.adsSpiConfigured = true;
    profile.adsDrdyConfigured = config.adsDrdy.configured;
    profile.adsRuntime = adsRuntimeFrom(config);

    odor::SensorManager sensorManager(primaryI2c, *h2sBus, adsSpi, &adsDrdy, profile);
    const odor::OperationResult beginResult = sensorManager.begin();
    std::cout << "Hardware access: enabled from runtime configuration\n";
    std::cout << "SensorManager initialization: " << initializationStatusText(beginResult) << '\n';

    const auto start = std::chrono::steady_clock::now();
    auto nextHeartbeat = start + odor::config::HeartbeatInterval;
    while (!shutdownRequested.load()) {
        const auto now = std::chrono::steady_clock::now();
        sensorManager.update(now);
        if (now >= nextHeartbeat) {
            const auto uptimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            std::cout << "heartbeat,uptime_ms=" << uptimeMs
                      << ",error_flags=" << sensorManager.errorFlags() << '\n';
            nextHeartbeat += odor::config::HeartbeatInterval;
        }
        std::this_thread::sleep_until(std::min(nextHeartbeat, now + std::chrono::milliseconds(50)));
    }

    std::cout << "Shutdown requested; exiting cleanly\n";
    return 0;
}
#endif

}  // namespace

int main(int argc, char** argv)
{
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const CommandLine options = parseCommandLine(argc, argv);

    std::cout << "Odor Sensing Raspberry Pi 5 Application\n";
    std::cout << "Version: " << odor::config::FirmwareScaffoldVersion << '\n';
    std::cout << "Build: " << __DATE__ << ' ' << __TIME__ << '\n';
    std::cout << "Target: " << odor::config::TargetName << '\n';
    std::cout << std::flush;

    if (options.forceMock) {
        return runMockApplication();
    }

    const odor::app::ConfigLoadResult loaded = odor::app::loadRuntimeConfig(options.configPath);
    if (!loaded.ok) {
        for (const std::string& error : loaded.errors) {
            std::cerr << "error: " << error << '\n';
        }
        std::cerr << "Use --mock to run without hardware access.\n";
        return 1;
    }

    odor::app::ValidationResult values = odor::app::validateRuntimeConfigValues(loaded.config);
    printValidation(values);
    if (!values.ok) {
        return 1;
    }

#if defined(ODOR_HAS_LINUX_HARDWARE)
    odor::app::ValidationResult hardware = odor::app::validateLinuxHardwareAccess(loaded.config);
    printValidation(hardware);
    if (!hardware.ok) {
        return 1;
    }

    std::cout << "Runtime configuration validated: " << options.configPath << '\n';
    if (options.validateOnly) {
        return 0;
    }
    return runLinuxApplication(loaded.config, options.diagnostic);
#else
    std::cerr << "warning: Linux hardware support is not built on this host; hardware validation skipped.\n";
    if (options.validateOnly) {
        return 0;
    }
    return runMockApplication();
#endif
}
