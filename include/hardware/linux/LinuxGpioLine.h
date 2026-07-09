#pragma once

#include <chrono>
#include <string>

#include "hardware/IGpioLine.h"

struct gpiod_chip;
struct gpiod_line;

namespace odor::hardware::linux {

enum class GpioDirection {
    Input,
    Output,
};

struct LinuxGpioConfig {
    std::string chipPath;
    unsigned int lineOffset = 0;
    std::string consumer = "odor-sensing";
    GpioDirection direction = GpioDirection::Input;
    GpioEdge edge = GpioEdge::None;
    bool activeLow = false;
    bool initialOutputValue = false;
};

class LinuxGpioLine : public IGpioLine {
public:
    explicit LinuxGpioLine(LinuxGpioConfig config);
    ~LinuxGpioLine() override;

    LinuxGpioLine(const LinuxGpioLine&) = delete;
    LinuxGpioLine& operator=(const LinuxGpioLine&) = delete;

    HardwareResult request();
    void release();

    bool isConfigured() const override;
    std::string description() const override;
    HardwareResult read(bool& value) override;
    HardwareResult write(bool value) override;
    HardwareResult waitForEdge(GpioEdge edge, std::chrono::milliseconds timeout) override;

private:
    LinuxGpioConfig config_;
    gpiod_chip* chip_ = nullptr;
    gpiod_line* line_ = nullptr;
};

}  // namespace odor::hardware::linux
