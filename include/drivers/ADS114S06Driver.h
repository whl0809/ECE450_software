#pragma once

#include "hardware/IGpioLine.h"
#include "hardware/ISPIDevice.h"
#include "config.h"
#include "sensor_types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace odor {

struct ADS114S06Config {
    bool spiDeviceConfigured = false;
    bool drdyConfigured = false;
    bool startConfigured = false;
    bool chipSelectPermanentlyAsserted = true;
    bool resetControlledByRaspberryPi = false;
    float referenceVoltageV = 2.5F;
    config::Ads114s06RuntimeSettings runtime{};
};

struct ADS114S06DiagnosticEvent {
    std::string stage;
    uint8_t registerAddress = 0;
    std::vector<uint8_t> txBytes;
    std::vector<uint8_t> rxBytes;
    std::vector<uint8_t> extractedRegisterBytes;
    bool hasChannel = false;
    size_t channelIndex = 0;
    uint8_t adsAin = 0;
    uint8_t inpmuxValue = 0;
    bool hasSample = false;
    int32_t rawCode = 0;
    float voltageV = 0.0F;
    bool hasComparison = false;
    uint8_t requestedWriteValue = 0;
    uint8_t extractedReadbackValue = 0;
    uint8_t readbackMask = 0xFF;
    uint8_t maskedExpected = 0;
    uint8_t maskedActual = 0;
    ErrorFlags errorFlags = 0;
};

using ADS114S06DiagnosticCallback = std::function<void(const ADS114S06DiagnosticEvent&)>;

class ADS114S06Driver {
public:
    ADS114S06Driver(hardware::ISPIDevice& spiDevice, const ADS114S06Config& config);
    ADS114S06Driver(hardware::ISPIDevice& spiDevice,
                    hardware::IGpioLine* drdyLine,
                    const ADS114S06Config& config);
    ADS114S06Driver(hardware::ISPIDevice& spiDevice,
                    hardware::IGpioLine* drdyLine,
                    hardware::IGpioLine* startLine,
                    const ADS114S06Config& config);

    OperationResult begin();
    DriverStatus status() const;
    OperationResult readTgsArray(TgsArrayMeasurement& measurement);
    OperationResult runResetRegisterSnapshotDiagnostic();
    void setDiagnosticCallback(ADS114S06DiagnosticCallback callback);

private:
    OperationResult readDeviceId();
    OperationResult writeRegister(uint8_t reg, uint8_t value, const std::string& stage);
    OperationResult readRegister(uint8_t reg, uint8_t& value, const std::string& stage);
    OperationResult writeRegisterChecked(uint8_t reg, uint8_t value);
    OperationResult holdStartLineLow(const std::string& stage);
    OperationResult verifyMaskedRegister(uint8_t reg,
                                         uint8_t requestedValue,
                                         uint8_t readbackMask,
                                         const std::string& stage);
    OperationResult configureReference();
    OperationResult configureRegisters();
    OperationResult selectChannel(uint8_t ain);
    OperationResult startConversion();
    OperationResult waitForReady();
    OperationResult readSample(size_t channelIndex,
                               uint8_t ain,
                               uint8_t inpmuxValue,
                               int32_t& rawCode,
                               float& voltageV);
    uint8_t pgaRegisterValue() const;
    uint8_t dataRateRegisterValue() const;
    uint8_t verificationMaskFor(uint8_t reg) const;
    void emitDiagnostic(ADS114S06DiagnosticEvent event) const;

    hardware::ISPIDevice& spiDevice_;
    hardware::IGpioLine* drdyLine_ = nullptr;
    hardware::IGpioLine* startLine_ = nullptr;
    ADS114S06Config config_;
    DriverStatus status_;
    ADS114S06DiagnosticCallback diagnosticCallback_;
};

}  // namespace odor
