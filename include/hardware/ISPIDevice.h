#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hardware/HardwareResult.h"

namespace odor::hardware {

class ISPIDevice {
public:
    virtual ~ISPIDevice() = default;

    virtual bool isConfigured() const = 0;
    virtual std::string description() const = 0;
    virtual HardwareResult transfer(const std::vector<uint8_t>& txBytes,
                                    std::vector<uint8_t>& rxBytes) = 0;
};

}  // namespace odor::hardware
