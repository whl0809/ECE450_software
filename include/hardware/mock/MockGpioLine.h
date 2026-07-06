#pragma once

#include <string>

#include "hardware/IGpioLine.h"

namespace odor::hardware::mock {

class MockGpioLine : public IGpioLine {
public:
    explicit MockGpioLine(bool configured = false, std::string name = "mock-gpio");

    bool isConfigured() const override;
    std::string description() const override;
    HardwareResult read(bool& value) override;
    HardwareResult write(bool value) override;
    HardwareResult waitForEdge(GpioEdge edge, std::chrono::milliseconds timeout) override;

    void setConfigured(bool configured);
    void setValue(bool value);
    void setNextWaitResult(HardwareResult result);
    void setPersistentWaitResult(HardwareResult result);
    void clearPersistentWaitResult();

private:
    bool configured_ = false;
    bool value_ = false;
    std::string name_;
    bool hasNextWaitResult_ = false;
    HardwareResult nextWaitResult_{};
    bool hasPersistentWaitResult_ = false;
    HardwareResult persistentWaitResult_{};
};

}  // namespace odor::hardware::mock
