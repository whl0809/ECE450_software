#include "drivers/ADS114S06Driver.h"

#include "error_flags.h"
#include "protocol/ProtocolUtils.h"

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace odor {

namespace {

// TI ADS114S06 command and register names from the ADS114S06 datasheet.
constexpr uint8_t AdsCommandReset = 0x06;
constexpr uint8_t AdsCommandStart = 0x08;
constexpr uint8_t AdsCommandRdata = 0x12;
constexpr uint8_t AdsCommandRreg = 0x20;
constexpr uint8_t AdsCommandWreg = 0x40;
constexpr uint8_t AdsRegisterId = 0x00;
constexpr uint8_t AdsRegisterInpmux = 0x02;
constexpr uint8_t AdsRegisterPga = 0x03;
constexpr uint8_t AdsRegisterDatarate = 0x04;
constexpr uint8_t AdsRegisterRef = 0x05;
constexpr uint8_t AdsAincom = 0x0C;
constexpr uint8_t AdsRequiredSpiMode = 1;
constexpr uint8_t AdsDeviceIdMask = 0x07;
constexpr uint8_t Ads114S06DeviceIdExpected = 0x05;
constexpr uint8_t AdsRefInternalAlwaysOn = 0x3A;
constexpr std::chrono::milliseconds AdsInternalReferenceSettleTime{10};
constexpr std::chrono::milliseconds AdsChannelSettleTime{80};
constexpr std::chrono::milliseconds AdsMinimumDrdyTimeout{120};

float adsGainValue(config::Ads114s06PgaGain gain)
{
    return static_cast<float>(static_cast<int>(gain));
}

uint8_t pgaGainCode(config::Ads114s06PgaGain gain)
{
    switch (gain) {
    case config::Ads114s06PgaGain::X1:
        return 0;
    case config::Ads114s06PgaGain::X2:
        return 1;
    case config::Ads114s06PgaGain::X4:
        return 2;
    case config::Ads114s06PgaGain::X8:
        return 3;
    case config::Ads114s06PgaGain::X16:
        return 4;
    case config::Ads114s06PgaGain::X32:
        return 5;
    case config::Ads114s06PgaGain::X64:
        return 6;
    case config::Ads114s06PgaGain::X128:
        return 7;
    }
    return 0;
}

OperationResult spiError()
{
    return {false, toErrorFlags(ErrorFlag::SpiFailure) |
                       toErrorFlags(ErrorFlag::CommunicationFailure)};
}

}  // namespace

ADS114S06Driver::ADS114S06Driver(hardware::ISPIDevice& spiDevice, const ADS114S06Config& config)
    : ADS114S06Driver(spiDevice, nullptr, nullptr, config)
{
}

ADS114S06Driver::ADS114S06Driver(hardware::ISPIDevice& spiDevice,
                                 hardware::IGpioLine* drdyLine,
                                 const ADS114S06Config& config)
    : ADS114S06Driver(spiDevice, drdyLine, nullptr, config)
{
}

ADS114S06Driver::ADS114S06Driver(hardware::ISPIDevice& spiDevice,
                                 hardware::IGpioLine* drdyLine,
                                 hardware::IGpioLine* startLine,
                                 const ADS114S06Config& config)
    : spiDevice_(spiDevice), drdyLine_(drdyLine), startLine_(startLine), config_(config)
{
}

OperationResult ADS114S06Driver::begin()
{
    if (!config_.spiDeviceConfigured || !spiDevice_.isConfigured()) {
        status_ = {false, false, toErrorFlags(ErrorFlag::DeviceNotConfigured)};
        return {false, status_.errorFlags};
    }
    if (config_.runtime.spiMode != AdsRequiredSpiMode || config_.runtime.bitsPerWord != 8U) {
        status_ = {true, false, toErrorFlags(ErrorFlag::InvalidConfiguration)};
        return {false, status_.errorFlags};
    }
    if (!config_.chipSelectPermanentlyAsserted || config_.resetControlledByRaspberryPi) {
        status_ = {true, false, toErrorFlags(ErrorFlag::InvalidConfiguration)};
        return {false, status_.errorFlags};
    }
    if (config_.drdyConfigured && (drdyLine_ == nullptr || !drdyLine_->isConfigured())) {
        status_ = {true, false, toErrorFlags(ErrorFlag::DeviceNotConfigured)};
        return {false, status_.errorFlags};
    }
    if (config_.startConfigured && (startLine_ == nullptr || !startLine_->isConfigured())) {
        status_ = {true, false, toErrorFlags(ErrorFlag::DeviceNotConfigured)};
        return {false, status_.errorFlags};
    }

    OperationResult result = holdStartLineLow("begin");
    if (!result.ok) {
        status_ = {true, false, result.errorFlags};
        return result;
    }

    result = readDeviceId();
    if (!result.ok) {
        status_ = {true, false, result.errorFlags};
        return result;
    }

    result = configureRegisters();
    if (!result.ok) {
        status_ = {true, false, result.errorFlags};
        return result;
    }

    result = startConversion();
    if (!result.ok) {
        status_ = {true, false, result.errorFlags};
        return result;
    }

    status_ = {true, true, 0};
    return {true, 0};
}

DriverStatus ADS114S06Driver::status() const
{
    return status_;
}

OperationResult ADS114S06Driver::readTgsArray(TgsArrayMeasurement& measurement)
{
    if (!status_.configured || !status_.detected || status_.errorFlags != 0) {
        measurement.valid = false;
        measurement.errorFlags = status_.errorFlags | toErrorFlags(ErrorFlag::DeviceNotConfigured);
        return {false, measurement.errorFlags};
    }

    ErrorFlags errors = 0;
    bool anyValid = false;
    for (size_t i = 0; i < config::TgsChannelCount; ++i) {
        const uint8_t ain = config::TgsChannels[i].ads114s06Ain;
        const uint8_t inpmuxValue = static_cast<uint8_t>((ain << 4U) | AdsAincom);
        OperationResult result = selectChannel(ain);
        if (!result.ok) {
            errors |= result.errorFlags;
            continue;
        }

        std::this_thread::sleep_for(AdsChannelSettleTime);

        int32_t rawCode = RawAdcUnavailable;
        float voltageV = 0.0F;
        result = readSample(i, ain, inpmuxValue, rawCode, voltageV);
        if (!result.ok) {
            errors |= result.errorFlags;
            continue;
        }

        measurement.adcRaw[i] = rawCode;
        measurement.voltageV[i] = voltageV;
        measurement.channelFresh[i] = true;
        anyValid = true;
        if (rawCode == 32767 || rawCode == -32768) {
            errors |= toErrorFlags(ErrorFlag::AdcSaturation);
        }
    }

    measurement.valid = anyValid && errors == 0;
    measurement.errorFlags = errors;
    measurement.monotonicTimestamp = std::chrono::steady_clock::now();
    measurement.wallTimestamp = std::chrono::system_clock::now();
    return {measurement.valid, errors};
}

void ADS114S06Driver::setDiagnosticCallback(ADS114S06DiagnosticCallback callback)
{
    diagnosticCallback_ = std::move(callback);
}

OperationResult ADS114S06Driver::runResetRegisterSnapshotDiagnostic()
{
    if (!config_.spiDeviceConfigured || !spiDevice_.isConfigured()) {
        return {false, toErrorFlags(ErrorFlag::DeviceNotConfigured)};
    }
    if (config_.startConfigured && (startLine_ == nullptr || !startLine_->isConfigured())) {
        return {false, toErrorFlags(ErrorFlag::DeviceNotConfigured)};
    }

    OperationResult startResult = holdStartLineLow("diagnostic_reset_snapshot");
    if (!startResult.ok) {
        return startResult;
    }

    std::vector<uint8_t> resetRx;
    const std::vector<uint8_t> resetTx = {AdsCommandReset};
    hardware::HardwareResult transferResult = spiDevice_.transfer(resetTx, resetRx);
    ADS114S06DiagnosticEvent resetEvent;
    resetEvent.stage = "reset_command";
    resetEvent.txBytes = resetTx;
    resetEvent.rxBytes = resetRx;
    if (!transferResult.ok) {
        resetEvent.errorFlags = spiError().errorFlags;
        emitDiagnostic(resetEvent);
        return spiError();
    }
    emitDiagnostic(resetEvent);

    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    std::vector<uint8_t> snapshotRx;
    const std::vector<uint8_t> snapshotTx = {
        static_cast<uint8_t>(AdsCommandRreg | AdsRegisterId),
        0x03,
        0x00,
        0x00,
        0x00,
        0x00,
    };
    transferResult = spiDevice_.transfer(snapshotTx, snapshotRx);

    ADS114S06DiagnosticEvent snapshotEvent;
    snapshotEvent.stage = "reset_register_snapshot_rreg";
    snapshotEvent.registerAddress = AdsRegisterId;
    snapshotEvent.txBytes = snapshotTx;
    snapshotEvent.rxBytes = snapshotRx;
    if (!transferResult.ok) {
        snapshotEvent.errorFlags = spiError().errorFlags;
        emitDiagnostic(snapshotEvent);
        return spiError();
    }
    if (snapshotRx.size() < 4U) {
        snapshotEvent.errorFlags = toErrorFlags(ErrorFlag::InvalidMeasurement);
        emitDiagnostic(snapshotEvent);
        return {false, snapshotEvent.errorFlags};
    }

    const size_t registerOffset = snapshotRx.size() - 4U;
    snapshotEvent.extractedRegisterBytes.assign(snapshotRx.begin() + static_cast<std::vector<uint8_t>::difference_type>(registerOffset),
                                                snapshotRx.end());
    const uint8_t idRegister = snapshotEvent.extractedRegisterBytes[0];
    snapshotEvent.hasComparison = true;
    snapshotEvent.requestedWriteValue = Ads114S06DeviceIdExpected;
    snapshotEvent.extractedReadbackValue = idRegister;
    snapshotEvent.readbackMask = AdsDeviceIdMask;
    snapshotEvent.maskedExpected = static_cast<uint8_t>(Ads114S06DeviceIdExpected & AdsDeviceIdMask);
    snapshotEvent.maskedActual = static_cast<uint8_t>(idRegister & AdsDeviceIdMask);
    if (snapshotEvent.maskedActual != snapshotEvent.maskedExpected) {
        snapshotEvent.errorFlags = toErrorFlags(ErrorFlag::DeviceNotDetected) |
                                   toErrorFlags(ErrorFlag::CommunicationFailure);
        emitDiagnostic(snapshotEvent);
        return {false, snapshotEvent.errorFlags};
    }

    emitDiagnostic(snapshotEvent);
    return {true, 0};
}

OperationResult ADS114S06Driver::holdStartLineLow(const std::string& stage)
{
    if (!config_.startConfigured) {
        return {true, 0};
    }
    if (startLine_ == nullptr || !startLine_->isConfigured()) {
        return {false, toErrorFlags(ErrorFlag::DeviceNotConfigured)};
    }

    const hardware::HardwareResult result = startLine_->write(false);
    if (!result.ok) {
        ADS114S06DiagnosticEvent event;
        event.stage = stage + "_start_line_low";
        event.errorFlags = toErrorFlags(ErrorFlag::CommunicationFailure);
        emitDiagnostic(event);
        return {false, event.errorFlags};
    }

    return {true, 0};
}

OperationResult ADS114S06Driver::readDeviceId()
{
    uint8_t readback = 0;
    OperationResult result = readRegister(AdsRegisterId, readback, "device_id_read");
    if (!result.ok) {
        return result;
    }

    const uint8_t maskedActual = static_cast<uint8_t>(readback & AdsDeviceIdMask);
    const uint8_t maskedExpected = static_cast<uint8_t>(Ads114S06DeviceIdExpected & AdsDeviceIdMask);
    ADS114S06DiagnosticEvent event;
    event.stage = "device_id_verify";
    event.registerAddress = AdsRegisterId;
    event.hasComparison = true;
    event.requestedWriteValue = Ads114S06DeviceIdExpected;
    event.extractedReadbackValue = readback;
    event.readbackMask = AdsDeviceIdMask;
    event.maskedExpected = maskedExpected;
    event.maskedActual = maskedActual;
    if (maskedActual != maskedExpected) {
        event.errorFlags = toErrorFlags(ErrorFlag::DeviceNotDetected) |
                           toErrorFlags(ErrorFlag::CommunicationFailure);
        emitDiagnostic(event);
        return {false, event.errorFlags};
    }

    emitDiagnostic(event);
    return {true, 0};
}

OperationResult ADS114S06Driver::writeRegister(uint8_t reg, uint8_t value, const std::string& stage)
{
    std::vector<uint8_t> ignored;
    const std::vector<uint8_t> tx = {
        static_cast<uint8_t>(AdsCommandWreg | (reg & 0x1FU)),
        0x00,
        value,
    };
    const hardware::HardwareResult result = spiDevice_.transfer(tx, ignored);
    ADS114S06DiagnosticEvent event;
    event.stage = stage + "_wreg";
    event.registerAddress = reg;
    event.txBytes = tx;
    event.rxBytes = ignored;
    event.requestedWriteValue = value;
    if (!result.ok) {
        event.errorFlags = spiError().errorFlags;
        emitDiagnostic(event);
        return spiError();
    }
    emitDiagnostic(event);
    return {true, 0};
}

OperationResult ADS114S06Driver::readRegister(uint8_t reg, uint8_t& value, const std::string& stage)
{
    std::vector<uint8_t> rx;
    const std::vector<uint8_t> tx = {
        static_cast<uint8_t>(AdsCommandRreg | (reg & 0x1FU)),
        0x00,
        0x00,
    };
    const hardware::HardwareResult result = spiDevice_.transfer(tx, rx);
    ADS114S06DiagnosticEvent event;
    event.stage = stage + "_rreg";
    event.registerAddress = reg;
    event.txBytes = tx;
    event.rxBytes = rx;
    if (!result.ok) {
        event.errorFlags = spiError().errorFlags;
        emitDiagnostic(event);
        return spiError();
    }
    if (rx.empty()) {
        event.errorFlags = toErrorFlags(ErrorFlag::InvalidMeasurement);
        emitDiagnostic(event);
        return {false, event.errorFlags};
    }
    value = rx.back();
    event.extractedReadbackValue = value;
    emitDiagnostic(event);
    return {true, 0};
}

OperationResult ADS114S06Driver::writeRegisterChecked(uint8_t reg, uint8_t value)
{
    OperationResult result = writeRegister(reg, value, "register_0x" + std::to_string(reg));
    if (!result.ok || !config_.runtime.verifyRegisterReadback) {
        return result;
    }

    return verifyMaskedRegister(reg,
                                value,
                                verificationMaskFor(reg),
                                "register_0x" + std::to_string(reg));
}

OperationResult ADS114S06Driver::verifyMaskedRegister(uint8_t reg,
                                                      uint8_t requestedValue,
                                                      uint8_t readbackMask,
                                                      const std::string& stage)
{
    uint8_t readback = 0;
    OperationResult result = readRegister(reg, readback, stage + "_verify");
    if (!result.ok) {
        return result;
    }

    const uint8_t maskedExpected = static_cast<uint8_t>(requestedValue & readbackMask);
    const uint8_t maskedActual = static_cast<uint8_t>(readback & readbackMask);
    ADS114S06DiagnosticEvent event;
    event.stage = stage + "_masked_compare";
    event.registerAddress = reg;
    event.hasComparison = true;
    event.requestedWriteValue = requestedValue;
    event.extractedReadbackValue = readback;
    event.readbackMask = readbackMask;
    event.maskedExpected = maskedExpected;
    event.maskedActual = maskedActual;
    if (maskedActual != maskedExpected) {
        event.errorFlags = toErrorFlags(ErrorFlag::InvalidConfiguration) |
                           toErrorFlags(ErrorFlag::CommunicationFailure);
        emitDiagnostic(event);
        return {false, event.errorFlags};
    }

    emitDiagnostic(event);
    return {true, 0};
}

OperationResult ADS114S06Driver::configureRegisters()
{
    OperationResult result = writeRegisterChecked(AdsRegisterPga, pgaRegisterValue());
    if (!result.ok) {
        return result;
    }
    result = writeRegisterChecked(AdsRegisterDatarate, dataRateRegisterValue());
    if (!result.ok) {
        return result;
    }

    return configureReference();
}

OperationResult ADS114S06Driver::configureReference()
{
    OperationResult result = writeRegister(AdsRegisterRef,
                                           AdsRefInternalAlwaysOn,
                                           "register_0x" + std::to_string(AdsRegisterRef));
    if (!result.ok) {
        return result;
    }

    std::this_thread::sleep_for(AdsInternalReferenceSettleTime);
    return verifyMaskedRegister(AdsRegisterRef,
                                AdsRefInternalAlwaysOn,
                                0xFF,
                                "register_0x" + std::to_string(AdsRegisterRef));
}

OperationResult ADS114S06Driver::selectChannel(uint8_t ain)
{
    if (ain > 11U) {
        return {false, toErrorFlags(ErrorFlag::InvalidConfiguration)};
    }
    const uint8_t muxValue = static_cast<uint8_t>((ain << 4U) | AdsAincom);
    return writeRegisterChecked(AdsRegisterInpmux, muxValue);
}

OperationResult ADS114S06Driver::startConversion()
{
    OperationResult startLineResult = holdStartLineLow("start_conversion");
    if (!startLineResult.ok) {
        return startLineResult;
    }

    std::vector<uint8_t> ignored;
    const std::vector<uint8_t> tx = {AdsCommandStart};
    const hardware::HardwareResult result = spiDevice_.transfer(tx, ignored);
    ADS114S06DiagnosticEvent event;
    event.stage = "start_conversion";
    event.txBytes = tx;
    event.rxBytes = ignored;
    if (!result.ok) {
        event.errorFlags = spiError().errorFlags;
        emitDiagnostic(event);
        return spiError();
    }
    emitDiagnostic(event);
    return {true, 0};
}

OperationResult ADS114S06Driver::waitForReady()
{
    if (!config_.drdyConfigured || !config_.runtime.waitForDrdyWhenConfigured) {
        return {true, 0};
    }
    if (drdyLine_ == nullptr || !drdyLine_->isConfigured()) {
        return {false, toErrorFlags(ErrorFlag::DeviceNotConfigured)};
    }

    bool drdyLevel = true;
    const hardware::HardwareResult levelResult = drdyLine_->read(drdyLevel);
    if (!levelResult.ok) {
        ADS114S06DiagnosticEvent event;
        event.stage = "wait_for_drdy_read_level";
        event.errorFlags = toErrorFlags(ErrorFlag::CommunicationFailure);
        emitDiagnostic(event);
        return {false, event.errorFlags};
    }
    if (!drdyLevel) {
        return {true, 0};
    }

    const std::chrono::milliseconds timeout =
        config_.runtime.conversionTimeout < AdsMinimumDrdyTimeout
            ? AdsMinimumDrdyTimeout
            : config_.runtime.conversionTimeout;
    const hardware::HardwareResult edgeResult =
        drdyLine_->waitForEdge(hardware::GpioEdge::Falling, timeout);
    if (!edgeResult.ok) {
        ADS114S06DiagnosticEvent event;
        event.stage = "wait_for_drdy";
        event.errorFlags = toErrorFlags(ErrorFlag::Timeout) | toErrorFlags(ErrorFlag::NotReady);
        emitDiagnostic(event);
        return {false, event.errorFlags};
    }
    return {true, 0};
}

OperationResult ADS114S06Driver::readSample(size_t channelIndex,
                                            uint8_t ain,
                                            uint8_t inpmuxValue,
                                            int32_t& rawCode,
                                            float& voltageV)
{
    std::vector<uint8_t> rx;
    const std::vector<uint8_t> tx = {AdsCommandRdata, 0x00, 0x00};
    const hardware::HardwareResult result = spiDevice_.transfer(tx, rx);
    ADS114S06DiagnosticEvent event;
    event.stage = "read_sample_rdata";
    event.txBytes = tx;
    event.rxBytes = rx;
    event.hasChannel = true;
    event.channelIndex = channelIndex;
    event.adsAin = ain;
    event.inpmuxValue = inpmuxValue;
    if (!result.ok) {
        event.errorFlags = spiError().errorFlags;
        emitDiagnostic(event);
        return spiError();
    }
    if (rx.size() < 3U) {
        event.errorFlags = toErrorFlags(ErrorFlag::InvalidMeasurement);
        emitDiagnostic(event);
        return {false, event.errorFlags};
    }

    rawCode = protocol::readSignedBe(rx, 1, 2);
    voltageV = (static_cast<float>(rawCode) / 32768.0F) *
               (config_.referenceVoltageV / adsGainValue(config_.runtime.pgaGain));
    event.hasSample = true;
    event.rawCode = rawCode;
    event.voltageV = voltageV;
    emitDiagnostic(event);
    return {true, 0};
}

uint8_t ADS114S06Driver::pgaRegisterValue() const
{
    return pgaGainCode(config_.runtime.pgaGain);
}

uint8_t ADS114S06Driver::dataRateRegisterValue() const
{
    return static_cast<uint8_t>((config_.runtime.filterCode & 0xE0U) |
                                (config_.runtime.dataRateCode & 0x1FU));
}

uint8_t ADS114S06Driver::verificationMaskFor(uint8_t reg) const
{
    switch (reg) {
    case AdsRegisterInpmux:
    case AdsRegisterPga:
    case AdsRegisterDatarate:
    case AdsRegisterRef:
        return 0xFF;
    default:
        return 0xFF;
    }
}

void ADS114S06Driver::emitDiagnostic(ADS114S06DiagnosticEvent event) const
{
    if (diagnosticCallback_) {
        diagnosticCallback_(event);
    }
}

}  // namespace odor
