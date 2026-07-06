#pragma once

#include <chrono>
#include <string>

#include "hardware/HardwareResult.h"

namespace odor::hardware {

enum class GpioEdge {
    None,
    Rising,
    Falling,
    Both,
};

class IGpioLine {
public:
    virtual ~IGpioLine() = default;

    virtual bool isConfigured() const = 0;
    virtual std::string description() const = 0;
    virtual HardwareResult read(bool& value) = 0;
    virtual HardwareResult write(bool value) = 0;
    virtual HardwareResult waitForEdge(GpioEdge edge, std::chrono::milliseconds timeout) = 0;
};

}  // namespace odor::hardware
