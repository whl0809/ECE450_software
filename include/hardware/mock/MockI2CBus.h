#pragma once

#include <string>
#include <vector>
#include <deque>

#include "hardware/II2CBus.h"

namespace odor::hardware::mock {

class MockI2CBus : public II2CBus {
public:
    explicit MockI2CBus(bool configured = false, std::string name = "mock-i2c");

    bool isConfigured() const override;
    std::string description() const override;
    HardwareResult write(uint8_t address, const std::vector<uint8_t>& bytes) override;
    HardwareResult read(uint8_t address, std::vector<uint8_t>& bytes) override;
    HardwareResult writeRead(uint8_t address,
                             const std::vector<uint8_t>& writeBytes,
                             std::vector<uint8_t>& readBytes) override;

    void setConfigured(bool configured);
    void setReadData(std::vector<uint8_t> data);
    void queueReadData(std::vector<uint8_t> data);
    void failNextTransfer(int errorCode, std::string message);
    const std::vector<uint8_t>& lastWrite() const;
    uint8_t lastAddress() const;

private:
    bool consumeFailure(HardwareResult& result);

    bool configured_ = false;
    std::string name_;
    std::vector<uint8_t> readData_;
    std::deque<std::vector<uint8_t>> queuedReadData_;
    std::vector<uint8_t> lastWrite_;
    uint8_t lastAddress_ = 0;
    bool failNext_ = false;
    int failCode_ = -1;
    std::string failMessage_;
};

}  // namespace odor::hardware::mock
