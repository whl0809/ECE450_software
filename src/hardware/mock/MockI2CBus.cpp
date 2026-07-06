#include "hardware/mock/MockI2CBus.h"

#include <utility>

namespace odor::hardware::mock {

MockI2CBus::MockI2CBus(bool configured, std::string name)
    : configured_(configured), name_(std::move(name))
{
}

bool MockI2CBus::isConfigured() const
{
    return configured_;
}

std::string MockI2CBus::description() const
{
    return name_;
}

HardwareResult MockI2CBus::write(uint8_t address, const std::vector<uint8_t>& bytes)
{
    if (!configured_) {
        return HardwareResult::failure(-1, "mock I2C bus is not configured");
    }
    HardwareResult failure;
    if (consumeFailure(failure)) {
        return failure;
    }
    lastAddress_ = address;
    lastWrite_ = bytes;
    return HardwareResult::success();
}

HardwareResult MockI2CBus::read(uint8_t address, std::vector<uint8_t>& bytes)
{
    if (!configured_) {
        return HardwareResult::failure(-1, "mock I2C bus is not configured");
    }
    HardwareResult failure;
    if (consumeFailure(failure)) {
        return failure;
    }
    lastAddress_ = address;
    if (!queuedReadData_.empty()) {
        bytes = queuedReadData_.front();
        queuedReadData_.pop_front();
    } else {
        bytes = readData_;
    }
    return HardwareResult::success();
}

HardwareResult MockI2CBus::writeRead(uint8_t address,
                                     const std::vector<uint8_t>& writeBytes,
                                     std::vector<uint8_t>& readBytes)
{
    if (!configured_) {
        return HardwareResult::failure(-1, "mock I2C bus is not configured");
    }
    HardwareResult failure;
    if (consumeFailure(failure)) {
        return failure;
    }
    lastAddress_ = address;
    lastWrite_ = writeBytes;
    if (!queuedReadData_.empty()) {
        readBytes = queuedReadData_.front();
        queuedReadData_.pop_front();
    } else {
        readBytes = readData_;
    }
    return HardwareResult::success();
}

void MockI2CBus::setConfigured(bool configured)
{
    configured_ = configured;
}

void MockI2CBus::setReadData(std::vector<uint8_t> data)
{
    readData_ = std::move(data);
}

void MockI2CBus::queueReadData(std::vector<uint8_t> data)
{
    queuedReadData_.push_back(std::move(data));
}

void MockI2CBus::failNextTransfer(int errorCode, std::string message)
{
    failNext_ = true;
    failCode_ = errorCode;
    failMessage_ = std::move(message);
}

const std::vector<uint8_t>& MockI2CBus::lastWrite() const
{
    return lastWrite_;
}

uint8_t MockI2CBus::lastAddress() const
{
    return lastAddress_;
}

bool MockI2CBus::consumeFailure(HardwareResult& result)
{
    if (!failNext_) {
        return false;
    }
    failNext_ = false;
    result = HardwareResult::failure(failCode_, failMessage_);
    return true;
}

}  // namespace odor::hardware::mock
