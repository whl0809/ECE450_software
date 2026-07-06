#include "hardware/linux/LinuxGpioLine.h"

#include <cerrno>
#include <cstring>
#include <gpiod.h>
#include <poll.h>
#include <utility>

namespace odor::hardware::linux {

namespace {

HardwareResult errnoFailure(const std::string& context)
{
    return HardwareResult::failure(-errno, context + ": " + std::strerror(errno));
}

}  // namespace

LinuxGpioLine::LinuxGpioLine(LinuxGpioConfig config)
    : config_(std::move(config))
{
}

LinuxGpioLine::~LinuxGpioLine()
{
    release();
}

HardwareResult LinuxGpioLine::request()
{
    if (config_.chipPath.empty()) {
        return HardwareResult::failure(-EINVAL, "GPIO chip path is not configured");
    }

    chip_ = gpiod_chip_open(config_.chipPath.c_str());
    if (chip_ == nullptr) {
        return errnoFailure("open GPIO chip");
    }

    line_ = gpiod_chip_get_line(chip_, config_.lineOffset);
    if (line_ == nullptr) {
        HardwareResult failed = errnoFailure("get GPIO line");
        release();
        return failed;
    }

    const int flags = config_.activeLow ? GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW : 0;
    int result = 0;
    if (config_.direction == GpioDirection::Output) {
        result = gpiod_line_request_output_flags(
            line_, config_.consumer.c_str(), flags, config_.initialOutputValue ? 1 : 0);
    } else {
        result = gpiod_line_request_input_flags(line_, config_.consumer.c_str(), flags);
    }

    if (result < 0) {
        HardwareResult failed = errnoFailure("request GPIO line");
        release();
        return failed;
    }

    return HardwareResult::success();
}

void LinuxGpioLine::release()
{
    if (line_ != nullptr) {
        gpiod_line_release(line_);
        line_ = nullptr;
    }
    if (chip_ != nullptr) {
        gpiod_chip_close(chip_);
        chip_ = nullptr;
    }
}

bool LinuxGpioLine::isConfigured() const
{
    return line_ != nullptr;
}

std::string LinuxGpioLine::description() const
{
    return config_.chipPath.empty() ? "unconfigured-linux-gpio" : config_.chipPath;
}

HardwareResult LinuxGpioLine::read(bool& value)
{
    if (line_ == nullptr) {
        return HardwareResult::failure(-ENODEV, "GPIO line is not requested");
    }
    const int result = gpiod_line_get_value(line_);
    if (result < 0) {
        return errnoFailure("read GPIO line");
    }
    value = result != 0;
    return HardwareResult::success();
}

HardwareResult LinuxGpioLine::write(bool value)
{
    if (line_ == nullptr) {
        return HardwareResult::failure(-ENODEV, "GPIO line is not requested");
    }
    if (gpiod_line_set_value(line_, value ? 1 : 0) < 0) {
        return errnoFailure("write GPIO line");
    }
    return HardwareResult::success();
}

HardwareResult LinuxGpioLine::waitForEdge(GpioEdge, std::chrono::milliseconds)
{
    return HardwareResult::failure(-ENOSYS, "GPIO edge waiting is not implemented yet");
}

}  // namespace odor::hardware::linux
