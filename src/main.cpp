#include <atomic>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "config.h"
#include "hardware/mock/MockI2CBus.h"
#include "hardware/mock/MockSPIDevice.h"
#include "services/SensorManager.h"

namespace {

std::atomic_bool shutdownRequested{false};

void handleSignal(int)
{
    shutdownRequested.store(true);
}

const char* initializationStatusText(const odor::OperationResult& result)
{
    return (result.ok && result.errorFlags == 0U) ? "OK" : "NOT CONFIGURED";
}

}  // namespace

int main()
{
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    odor::hardware::mock::MockI2CBus primaryI2c(false, "unconfigured-primary-i2c");
    odor::hardware::mock::MockI2CBus secondaryI2c(false, "unconfigured-secondary-i2c");
    odor::hardware::mock::MockSPIDevice adsSpi(false, "unconfigured-ads114s06-spi");
    odor::SensorManager sensorManager(primaryI2c, secondaryI2c, adsSpi);

    std::cout << "Odor Sensing Raspberry Pi 5 Application\n";
    std::cout << "Version: " << odor::config::FirmwareScaffoldVersion << '\n';
    std::cout << "Build: " << __DATE__ << ' ' << __TIME__ << '\n';
    std::cout << "Target: " << odor::config::TargetName << '\n';
    std::cout << "Hardware access: disabled until runtime device paths are configured\n";

    const odor::OperationResult beginResult = sensorManager.begin();
    std::cout << "SensorManager initialization: " << initializationStatusText(beginResult) << '\n';
    std::cout << "Sensor interfaces are not yet enabled\n";

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

    std::cout << "Shutdown requested; exiting cleanly\n";
    return 0;
}
