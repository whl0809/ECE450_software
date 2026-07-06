#pragma once

#include <cstdint>
#include <string>

#include "hardware/ISPIDevice.h"

namespace odor::hardware::linux {

struct LinuxSPIConfig {
    std::string devicePath;
    uint8_t mode = 0;
    uint8_t bitsPerWord = 8;
    uint32_t maxSpeedHz = 0;
};

class LinuxSPIDevice : public ISPIDevice {
public:
    explicit LinuxSPIDevice(LinuxSPIConfig config);
    ~LinuxSPIDevice() override;

    LinuxSPIDevice(const LinuxSPIDevice&) = delete;
    LinuxSPIDevice& operator=(const LinuxSPIDevice&) = delete;

    HardwareResult open();
    void close();

    bool isConfigured() const override;
    std::string description() const override;
    HardwareResult transfer(const std::vector<uint8_t>& txBytes,
                            std::vector<uint8_t>& rxBytes) override;

private:
    LinuxSPIConfig config_;
    int fd_ = -1;
};

}  // namespace odor::hardware::linux
