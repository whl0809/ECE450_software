#include "drivers/ADS114S06Driver.h"

#include "error_flags.h"
#include "protocol/ProtocolUtils.h"

#include <cmath>
#include <vector>

namespace odor {

namespace {

// TI ADS114S06 command and register names from the ADS114S06 datasheet.
constexpr uint8_t AdsCommandStart = 0x08;
constexpr uint8_t AdsCommandRdata = 0x12;
constexpr uint8_t AdsCommandRreg = 0x20;
constexpr uint8_t AdsCommandWreg = 0x40;
constexpr uint8_t AdsRegisterInpmux = 0x02;
constexpr uint8_t AdsRegisterPga = 0x03;
constexpr uint8_t AdsRegisterDatarate = 0x04;
constexpr uint8_t AdsRegisterRef = 0x05;
constexpr uint8_t AdsAincom = 0x0C;
constexpr uint8_t AdsRequiredSpiMode = 1;

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
    : spiDevice_(spiDevice), config_(config)
{
}

ADS114S06Driver::ADS114S06Driver(hardware::ISPIDevice& spiDevice,
                                 hardware::IGpioLine* drdyLine,
                                 const ADS114S06Config& config)
    : spiDevice_(spiDevice), drdyLine_(drdyLine), config_(config)
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

    OperationResult result = configureRegisters();
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
        OperationResult result = selectChannel(ain);
        if (!result.ok) {
            errors |= result.errorFlags;
            continue;
        }

        result = startConversion();
        if (!result.ok) {
            errors |= result.errorFlags;
            continue;
        }

        result = waitForReady();
        if (!result.ok) {
            errors |= result.errorFlags;
            continue;
        }

        int32_t rawCode = RawAdcUnavailable;
        result = readSample(rawCode);
        if (!result.ok) {
            errors |= result.errorFlags;
            continue;
        }

        measurement.adcRaw[i] = rawCode;
        measurement.voltageV[i] =
            (static_cast<float>(rawCode) / 32768.0F) *
            (config_.referenceVoltageV / adsGainValue(config_.runtime.pgaGain));
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

OperationResult ADS114S06Driver::writeRegister(uint8_t reg, uint8_t value)
{
    std::vector<uint8_t> ignored;
    const std::vector<uint8_t> tx = {
        static_cast<uint8_t>(AdsCommandWreg | (reg & 0x1FU)),
        0x00,
        value,
    };
    const hardware::HardwareResult result = spiDevice_.transfer(tx, ignored);
    if (!result.ok) {
        return spiError();
    }
    return {true, 0};
}

OperationResult ADS114S06Driver::readRegister(uint8_t reg, uint8_t& value)
{
    std::vector<uint8_t> rx;
    const std::vector<uint8_t> tx = {
        static_cast<uint8_t>(AdsCommandRreg | (reg & 0x1FU)),
        0x00,
        0x00,
    };
    const hardware::HardwareResult result = spiDevice_.transfer(tx, rx);
    if (!result.ok) {
        return spiError();
    }
    if (rx.empty()) {
        return {false, toErrorFlags(ErrorFlag::InvalidMeasurement)};
    }
    value = rx.back();
    return {true, 0};
}

OperationResult ADS114S06Driver::writeRegisterChecked(uint8_t reg, uint8_t value)
{
    OperationResult result = writeRegister(reg, value);
    if (!result.ok || !config_.runtime.verifyRegisterReadback) {
        return result;
    }

    uint8_t readback = 0;
    result = readRegister(reg, readback);
    if (!result.ok) {
        return result;
    }
    if (readback != value) {
        return {false, toErrorFlags(ErrorFlag::InvalidConfiguration) |
                           toErrorFlags(ErrorFlag::CommunicationFailure)};
    }
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

    // REF register value 0 selects the external REFP0/REFN0 path and does not
    // enable an internal reference; board hardware provides the 4.096 V REF5040.
    return writeRegisterChecked(AdsRegisterRef, 0x00);
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
    std::vector<uint8_t> ignored;
    const hardware::HardwareResult result = spiDevice_.transfer({AdsCommandStart}, ignored);
    if (!result.ok) {
        return spiError();
    }
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
    const hardware::HardwareResult result =
        drdyLine_->waitForEdge(hardware::GpioEdge::Falling, config_.runtime.conversionTimeout);
    if (!result.ok) {
        return {false, toErrorFlags(ErrorFlag::Timeout) | toErrorFlags(ErrorFlag::NotReady)};
    }
    return {true, 0};
}

OperationResult ADS114S06Driver::readSample(int32_t& rawCode)
{
    std::vector<uint8_t> rx;
    const hardware::HardwareResult result = spiDevice_.transfer({AdsCommandRdata, 0x00, 0x00}, rx);
    if (!result.ok) {
        return spiError();
    }
    if (rx.size() < 2U) {
        return {false, toErrorFlags(ErrorFlag::InvalidMeasurement)};
    }

    const size_t offset = rx.size() - 2U;
    rawCode = protocol::readSignedBe(rx, offset, 2);
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

}  // namespace odor
