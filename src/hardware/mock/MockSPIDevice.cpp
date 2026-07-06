#include "hardware/mock/MockSPIDevice.h"

#include <utility>

namespace odor::hardware::mock {

MockSPIDevice::MockSPIDevice(bool configured, std::string name)
    : configured_(configured), name_(std::move(name))
{
}

bool MockSPIDevice::isConfigured() const
{
    return configured_;
}

std::string MockSPIDevice::description() const
{
    return name_;
}

HardwareResult MockSPIDevice::transfer(const std::vector<uint8_t>& txBytes,
                                       std::vector<uint8_t>& rxBytes)
{
    if (!configured_) {
        return HardwareResult::failure(-1, "mock SPI device is not configured");
    }
    HardwareResult failure;
    if (consumeFailure(failure)) {
        return failure;
    }
    lastTx_ = txBytes;
    if (!queuedRxData_.empty()) {
        rxBytes = queuedRxData_.front();
        queuedRxData_.pop_front();
    } else {
        rxBytes = rxData_.empty() ? std::vector<uint8_t>(txBytes.size(), 0U) : rxData_;
    }
    return HardwareResult::success();
}

void MockSPIDevice::setConfigured(bool configured)
{
    configured_ = configured;
}

void MockSPIDevice::setRxData(std::vector<uint8_t> data)
{
    rxData_ = std::move(data);
}

void MockSPIDevice::queueRxData(std::vector<uint8_t> data)
{
    queuedRxData_.push_back(std::move(data));
}

void MockSPIDevice::failNextTransfer(int errorCode, std::string message)
{
    failNext_ = true;
    failCode_ = errorCode;
    failMessage_ = std::move(message);
}

const std::vector<uint8_t>& MockSPIDevice::lastTx() const
{
    return lastTx_;
}

bool MockSPIDevice::consumeFailure(HardwareResult& result)
{
    if (!failNext_) {
        return false;
    }
    failNext_ = false;
    result = HardwareResult::failure(failCode_, failMessage_);
    return true;
}

}  // namespace odor::hardware::mock
