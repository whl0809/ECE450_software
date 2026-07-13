#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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

std::string bytesToHex(const std::vector<uint8_t>& bytes)
{
    std::ostringstream out;
    out << '[';
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i != 0U) {
            out << ' ';
        }
        out << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(bytes[i]);
    }
    out << ']';
    return out.str();
}

void printAdsDiagnosticEvent(const odor::ADS114S06DiagnosticEvent& event)
{
    std::cout << "ads_diag"
              << ",stage=" << event.stage
              << ",reg=0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
              << static_cast<int>(event.registerAddress)
              << std::dec
              << ",tx=" << bytesToHex(event.txBytes)
              << ",rx=" << bytesToHex(event.rxBytes);
    if (!event.extractedRegisterBytes.empty()) {
        std::cout << ",registers_0x00_0x03=" << bytesToHex(event.extractedRegisterBytes);
        if (event.extractedRegisterBytes.size() >= 4U) {
            const uint8_t status = event.extractedRegisterBytes[1];
            const uint8_t inpmux = event.extractedRegisterBytes[2];
            const uint8_t pga = event.extractedRegisterBytes[3];
            std::cout << ",status_expected=0x80,status_actual=0x" << std::hex << std::uppercase
                      << std::setw(2) << std::setfill('0') << static_cast<int>(status)
                      << ",status_matches=" << (status == 0x80U ? "true" : "false")
                      << ",inpmux_expected=0x01,inpmux_actual=0x" << std::setw(2)
                      << static_cast<int>(inpmux)
                      << ",inpmux_matches=" << (inpmux == 0x01U ? "true" : "false")
                      << ",pga_expected=0x00,pga_actual=0x" << std::setw(2)
                      << static_cast<int>(pga)
                      << ",pga_matches=" << (pga == 0x00U ? "true" : "false")
                      << std::dec;
        }
    }
    if (event.hasChannel) {
        std::cout << ",channel=" << event.channelIndex
                  << ",ain=" << static_cast<int>(event.adsAin)
                  << ",inpmux=0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                  << static_cast<int>(event.inpmuxValue)
                  << std::dec;
    }
    if (event.hasSample) {
        std::cout << ",raw=" << event.rawCode
                  << ",voltage_v=" << event.voltageV;
    }
    if (event.hasComparison) {
        std::cout << ",requested=0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                  << static_cast<int>(event.requestedWriteValue)
                  << ",readback=0x" << std::setw(2) << static_cast<int>(event.extractedReadbackValue)
                  << ",mask=0x" << std::setw(2) << static_cast<int>(event.readbackMask)
                  << ",masked_expected=0x" << std::setw(2) << static_cast<int>(event.maskedExpected)
                  << ",masked_actual=0x" << std::setw(2) << static_cast<int>(event.maskedActual)
                  << std::dec;
    }
    std::cout << ",error_flags=" << event.errorFlags << '\n';
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
                          odor::hardware::IGpioLine& adsStart,
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
                              &adsStart,
                              {true,
                               config.adsDrdy.configured,
                               config.adsStart.configured,
                               odor::config::Ads114s06ChipSelectPermanentlyAsserted,
                               odor::config::Ads114s06ResetControlledByRaspberryPi,
                               config.adsReferenceVoltageV,
                               adsRuntime});

    int failures = 0;
    auto record = [&](const std::string& name, const odor::OperationResult& result) {
        printDiagnosticResult(name, result);
        if (!result.ok) {
            ++failures;
        }
    };

    odor::Sht45Measurement sht45Measurement;
    if (config.enableSht45) {
        record("sht45_begin", sht45.begin());
        record("sht45_read", sht45.readMeasurement(sht45Measurement));
    }

    if (config.enableSgp41) {
        record("sgp41_begin", sgp41.begin());
        if (sht45Measurement.valid) {
            sgp41.setCompensation(sht45Measurement.temperatureC,
                                  sht45Measurement.humidityRh,
                                  sht45Measurement.monotonicTimestamp);
        }
        odor::Sgp41Measurement sgp41Measurement;
        record("sgp41_read_raw", sgp41.readRawSignals(sgp41Measurement));
    }

    if (config.enableBme690) {
        record("bme690_begin", bme690.begin());
        odor::Bme690Measurement bme690Measurement;
        record("bme690_read", bme690.readMeasurement(bme690Measurement));
    }

    if (config.enableNh3Mcp3421) {
        record("nh3_mcp3421_begin", nh3.begin());
        odor::ElectrochemicalMeasurement nh3Measurement;
        record("nh3_mcp3421_read", nh3.readElectrochemical(nh3Measurement));
    }

    if (config.enableH2sMcp3421) {
        record("h2s_mcp3421_begin", h2s.begin());
        odor::ElectrochemicalMeasurement h2sMeasurement;
        record("h2s_mcp3421_read", h2s.readElectrochemical(h2sMeasurement));
    }

    if (config.enableAds114s06) {
        ads.setDiagnosticCallback(printAdsDiagnosticEvent);
        record("ads114s06_reset_register_snapshot", ads.runResetRegisterSnapshotDiagnostic());
        record("ads114s06_begin", ads.begin());
        odor::TgsArrayMeasurement tgsMeasurement;
        record("ads114s06_read_tgs_array", ads.readTgsArray(tgsMeasurement));
    }

    return failures == 0 ? 0 : 2;
}

int runLinuxApplication(const odor::app::RuntimeConfig& config, bool diagnostic)
{
    const bool anyI2cSensor = config.enableSht45 || config.enableSgp41 ||
                              config.enableBme690 || config.enableNh3Mcp3421 ||
                              config.enableH2sMcp3421;

    odor::hardware::mock::MockI2CBus disabledI2c(false, "disabled-i2c");
    std::unique_ptr<odor::hardware::linux::LinuxI2CBus> primaryI2c;
    odor::hardware::II2CBus* primaryBus = &disabledI2c;
    if (anyI2cSensor) {
        primaryI2c = std::make_unique<odor::hardware::linux::LinuxI2CBus>(config.primaryI2c.path);
        const odor::hardware::HardwareResult primaryOpen = primaryI2c->open();
        if (!primaryOpen.ok) {
            std::cerr << "error: " << primaryOpen.message << '\n';
            return 1;
        }
        primaryBus = primaryI2c.get();
    }

    std::unique_ptr<odor::hardware::linux::LinuxI2CBus> secondaryI2c;
    odor::hardware::II2CBus* h2sBus = primaryBus;
    if (anyI2cSensor && !config.secondaryI2c.path.empty() && config.secondaryI2c.path != config.primaryI2c.path) {
        secondaryI2c = std::make_unique<odor::hardware::linux::LinuxI2CBus>(config.secondaryI2c.path);
        const odor::hardware::HardwareResult secondaryOpen = secondaryI2c->open();
        if (!secondaryOpen.ok) {
            std::cerr << "error: " << secondaryOpen.message << '\n';
            return 1;
        }
        h2sBus = secondaryI2c.get();
    }

    odor::hardware::mock::MockSPIDevice disabledSpi(false, "disabled-spi");
    std::unique_ptr<odor::hardware::linux::LinuxSPIDevice> adsSpi;
    odor::hardware::ISPIDevice* adsSpiDevice = &disabledSpi;
    odor::hardware::mock::MockGpioLine disabledDrdy(false, "disabled-drdy");
    odor::hardware::IGpioLine* adsDrdyLine = &disabledDrdy;
    std::unique_ptr<odor::hardware::linux::LinuxGpioLine> adsStart;
    std::unique_ptr<odor::hardware::linux::LinuxGpioLine> adsDrdy;

    if (config.enableAds114s06) {
        adsSpi = std::make_unique<odor::hardware::linux::LinuxSPIDevice>(odor::hardware::linux::LinuxSPIConfig{
            config.adsSpiDevice,
            config.adsSpiMode,
            config.adsBitsPerWord,
            config.adsMaxSpeedHz,
        });
        const odor::hardware::HardwareResult spiOpen = adsSpi->open();
        if (!spiOpen.ok) {
            std::cerr << "error: " << spiOpen.message << '\n';
            return 1;
        }
        adsSpiDevice = adsSpi.get();
        std::cout << "spi_actual,mode=" << static_cast<int>(adsSpi->actualMode())
                  << ",bits_per_word=" << static_cast<int>(adsSpi->actualBitsPerWord())
                  << ",max_speed_hz=" << adsSpi->actualMaxSpeedHz() << '\n';

        adsStart = std::make_unique<odor::hardware::linux::LinuxGpioLine>(odor::hardware::linux::LinuxGpioConfig{
            config.gpioChipPath,
            config.adsStart.lineOffset,
            "odor-sensing-ads-start",
            odor::hardware::linux::GpioDirection::Output,
            odor::hardware::GpioEdge::None,
            config.adsStart.activeLow,
            false,
        });
        const odor::hardware::HardwareResult startRequest = adsStart->request();
        if (!startRequest.ok) {
            std::cerr << "error: " << startRequest.message << '\n';
            return 1;
        }

        adsDrdy = std::make_unique<odor::hardware::linux::LinuxGpioLine>(odor::hardware::linux::LinuxGpioConfig{
            config.gpioChipPath,
            config.adsDrdy.lineOffset,
            "odor-sensing-ads-drdy",
            odor::hardware::linux::GpioDirection::Input,
            odor::hardware::GpioEdge::Falling,
            config.adsDrdy.activeLow,
            false,
        });
        const odor::hardware::HardwareResult drdyRequest = adsDrdy->request();
        if (!drdyRequest.ok) {
            std::cerr << "error: " << drdyRequest.message << '\n';
            return 1;
        }
        adsDrdyLine = adsDrdy.get();

        if (diagnostic) {
            return runHardwareDiagnostic(*primaryBus, *h2sBus, *adsSpiDevice, *adsDrdyLine, *adsStart, config);
        }
    }

    odor::SensorManagerRuntimeProfile profile;
    profile.i2cBusAssignmentsConfirmed = true;
    profile.adsSpiConfigured = config.enableAds114s06;
    profile.adsStartConfigured = config.adsStart.configured;
    profile.adsDrdyConfigured = config.adsDrdy.configured;
    profile.enableTgsArray = config.enableAds114s06;
    profile.enableNh3Sensor = config.enableNh3Mcp3421;
    profile.enableH2sSensor = config.enableH2sMcp3421;
    profile.enableSgp41 = config.enableSgp41;
    profile.enableBme690 = config.enableBme690;
    profile.enableSht45 = config.enableSht45;
    profile.adsReferenceVoltageV = config.adsReferenceVoltageV;
    profile.adsRuntime = adsRuntimeFrom(config);

    odor::SensorManager sensorManager(*primaryBus, *h2sBus, *adsSpiDevice, adsDrdyLine, adsStart.get(), profile);
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
