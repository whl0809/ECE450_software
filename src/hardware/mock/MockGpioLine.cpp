#include "hardware/mock/MockGpioLine.h"

#include <utility>

namespace odor::hardware::mock {

MockGpioLine::MockGpioLine(bool configured, std::string name)
    : configured_(configured), name_(std::move(name))
{
}

bool MockGpioLine::isConfigured() const
{
    return configured_;
}

std::string MockGpioLine::description() const
{
    return name_;
}

HardwareResult MockGpioLine::read(bool& value)
{
    if (!configured_) {
        return HardwareResult::failure(-1, "mock GPIO line is not configured");
    }
    value = value_;
    return HardwareResult::success();
}

HardwareResult MockGpioLine::write(bool value)
{
    if (!configured_) {
        return HardwareResult::failure(-1, "mock GPIO line is not configured");
    }
    value_ = value;
    wasEverWrittenHigh_ = wasEverWrittenHigh_ || value;
    ++writeCount_;
    return HardwareResult::success();
}

HardwareResult MockGpioLine::waitForEdge(GpioEdge, std::chrono::milliseconds)
{
    if (hasNextWaitResult_) {
        hasNextWaitResult_ = false;
        return nextWaitResult_;
    }
    if (hasPersistentWaitResult_) {
        return persistentWaitResult_;
    }
    return configured_ ? HardwareResult::success()
                       : HardwareResult::failure(-1, "mock GPIO line is not configured");
}

void MockGpioLine::setConfigured(bool configured)
{
    configured_ = configured;
}

void MockGpioLine::setValue(bool value)
{
    value_ = value;
}

void MockGpioLine::setNextWaitResult(HardwareResult result)
{
    nextWaitResult_ = std::move(result);
    hasNextWaitResult_ = true;
}

void MockGpioLine::setPersistentWaitResult(HardwareResult result)
{
    persistentWaitResult_ = std::move(result);
    hasPersistentWaitResult_ = true;
}

void MockGpioLine::clearPersistentWaitResult()
{
    hasPersistentWaitResult_ = false;
}

bool MockGpioLine::wasEverWrittenHigh() const
{
    return wasEverWrittenHigh_;
}

size_t MockGpioLine::writeCount() const
{
    return writeCount_;
}

}  // namespace odor::hardware::mock
