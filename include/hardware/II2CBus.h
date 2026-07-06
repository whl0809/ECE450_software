#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hardware/HardwareResult.h"

namespace odor::hardware {

class II2CBus {
public:
    virtual ~II2CBus() = default;

    virtual bool isConfigured() const = 0;
    virtual std::string description() const = 0;
    virtual HardwareResult write(uint8_t address, const std::vector<uint8_t>& bytes) = 0;
    virtual HardwareResult read(uint8_t address, std::vector<uint8_t>& bytes) = 0;
    virtual HardwareResult writeRead(uint8_t address,
                                     const std::vector<uint8_t>& writeBytes,
                                     std::vector<uint8_t>& readBytes) = 0;
};

}  // namespace odor::hardware
