#pragma once

#include <string>
#include <vector>
#include <deque>

#include "hardware/ISPIDevice.h"

namespace odor::hardware::mock {

class MockSPIDevice : public ISPIDevice {
public:
    explicit MockSPIDevice(bool configured = false, std::string name = "mock-spi");

    bool isConfigured() const override;
    std::string description() const override;
    HardwareResult transfer(const std::vector<uint8_t>& txBytes,
                            std::vector<uint8_t>& rxBytes) override;

    void setConfigured(bool configured);
    void setRxData(std::vector<uint8_t> data);
    void queueRxData(std::vector<uint8_t> data);
    void failNextTransfer(int errorCode, std::string message);
    const std::vector<uint8_t>& lastTx() const;

private:
    bool consumeFailure(HardwareResult& result);

    bool configured_ = false;
    std::string name_;
    std::vector<uint8_t> rxData_;
    std::deque<std::vector<uint8_t>> queuedRxData_;
    std::vector<uint8_t> lastTx_;
    bool failNext_ = false;
    int failCode_ = -1;
    std::string failMessage_;
};

}  // namespace odor::hardware::mock
