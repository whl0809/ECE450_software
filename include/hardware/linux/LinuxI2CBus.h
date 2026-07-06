#pragma once

#include <string>

#include "hardware/II2CBus.h"

namespace odor::hardware::linux {

class LinuxI2CBus : public II2CBus {
public:
    explicit LinuxI2CBus(std::string adapterPath);
    ~LinuxI2CBus() override;

    LinuxI2CBus(const LinuxI2CBus&) = delete;
    LinuxI2CBus& operator=(const LinuxI2CBus&) = delete;

    HardwareResult open();
    void close();

    bool isConfigured() const override;
    std::string description() const override;
    HardwareResult write(uint8_t address, const std::vector<uint8_t>& bytes) override;
    HardwareResult read(uint8_t address, std::vector<uint8_t>& bytes) override;
    HardwareResult writeRead(uint8_t address,
                             const std::vector<uint8_t>& writeBytes,
                             std::vector<uint8_t>& readBytes) override;

private:
    HardwareResult selectAddress(uint8_t address);

    std::string adapterPath_;
    int fd_ = -1;
};

}  // namespace odor::hardware::linux
