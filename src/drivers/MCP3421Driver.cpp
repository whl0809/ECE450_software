#include "drivers/MCP3421Driver.h"

#include "error_flags.h"
#include "protocol/ProtocolUtils.h"

#include <cmath>
#include <vector>

namespace odor {

namespace {

uint8_t resolutionCode(config::Mcp3421Resolution resolution)
{
    switch (resolution) {
    case config::Mcp3421Resolution::Bits12:
        return 0x00;
    case config::Mcp3421Resolution::Bits14:
        return 0x01;
    case config::Mcp3421Resolution::Bits16:
        return 0x02;
    case config::Mcp3421Resolution::Bits18:
        return 0x03;
    }
    return 0x02;
}

uint8_t gainCode(config::Mcp3421Gain gain)
{
    switch (gain) {
    case config::Mcp3421Gain::X1:
        return 0x00;
    case config::Mcp3421Gain::X2:
        return 0x01;
    case config::Mcp3421Gain::X4:
        return 0x02;
    case config::Mcp3421Gain::X8:
        return 0x03;
    }
    return 0x00;
}

float gainValue(config::Mcp3421Gain gain)
{
    switch (gain) {
    case config::Mcp3421Gain::X1:
        return 1.0F;
    case config::Mcp3421Gain::X2:
        return 2.0F;
    case config::Mcp3421Gain::X4:
        return 4.0F;
    case config::Mcp3421Gain::X8:
        return 8.0F;
    }
    return 1.0F;
}

float lsbV(config::Mcp3421Resolution resolution, config::Mcp3421Gain gain)
{
    switch (resolution) {
    case config::Mcp3421Resolution::Bits12:
        return 0.001F / gainValue(gain);
    case config::Mcp3421Resolution::Bits14:
        return 0.000250F / gainValue(gain);
    case config::Mcp3421Resolution::Bits16:
        return 0.0000625F / gainValue(gain);
    case config::Mcp3421Resolution::Bits18:
        return 0.000015625F / gainValue(gain);
    }
    return 0.0000625F;
}

uint8_t configByte(const config::Mcp3421RuntimeSettings& settings, bool startConversion)
{
    uint8_t value = startConversion ? 0x80U : 0x00U;
    if (settings.conversionMode == config::Mcp3421ConversionMode::Continuous) {
        value |= 0x10U;
    }
    value |= static_cast<uint8_t>(resolutionCode(settings.resolution) << 2U);
    value |= gainCode(settings.gain);
    return value;
}

}  // namespace

MCP3421Driver::MCP3421Driver(hardware::II2CBus& bus, const MCP3421Config& config)
    : bus_(bus), config_(config)
{
}

OperationResult MCP3421Driver::begin()
{
    if (!config_.addressConfigured || !config_.busAssignmentConfirmed || !bus_.isConfigured()) {
        status_ = {false, false, toErrorFlags(ErrorFlag::DeviceNotConfigured)};
        return {false, status_.errorFlags};
    }

    status_ = {true, true, 0};
    return {true, 0};
}

DriverStatus MCP3421Driver::status() const
{
    return status_;
}

OperationResult MCP3421Driver::readElectrochemical(ElectrochemicalMeasurement& measurement)
{
    if (!status_.configured || !status_.detected || status_.errorFlags != 0) {
        measurement.valid = false;
        measurement.errorFlags = status_.errorFlags | toErrorFlags(ErrorFlag::DeviceNotConfigured);
        return {false, measurement.errorFlags};
    }

    if (config_.runtime.conversionMode == config::Mcp3421ConversionMode::OneShot) {
        const std::vector<uint8_t> start = {configByte(config_.runtime, true)};
        const hardware::HardwareResult writeResult = bus_.write(config_.i2cAddress, start);
        if (!writeResult.ok) {
            measurement.errorFlags = toErrorFlags(ErrorFlag::I2cFailure) |
                                     toErrorFlags(ErrorFlag::CommunicationFailure);
            return {false, measurement.errorFlags};
        }
    }

    const size_t dataBytes = (config_.runtime.resolution == config::Mcp3421Resolution::Bits18) ? 3U : 2U;
    std::vector<uint8_t> response(dataBytes + 1U);
    const hardware::HardwareResult readResult = bus_.read(config_.i2cAddress, response);
    if (!readResult.ok) {
        measurement.errorFlags = toErrorFlags(ErrorFlag::I2cFailure) |
                                 toErrorFlags(ErrorFlag::CommunicationFailure);
        return {false, measurement.errorFlags};
    }
    if (response.size() != dataBytes + 1U) {
        measurement.errorFlags = toErrorFlags(ErrorFlag::InvalidMeasurement);
        return {false, measurement.errorFlags};
    }

    const uint8_t receivedConfig = response.back();
    if ((receivedConfig & 0x80U) != 0U) {
        measurement.errorFlags = toErrorFlags(ErrorFlag::NotReady) |
                                 toErrorFlags(ErrorFlag::Timeout);
        return {false, measurement.errorFlags};
    }

    measurement.adcRaw = protocol::readSignedBe(response, 0, dataBytes);
    measurement.differentialVoltageV =
        static_cast<float>(measurement.adcRaw) * lsbV(config_.runtime.resolution, config_.runtime.gain);
    measurement.frontEndVoltageV = measurement.differentialVoltageV;
    measurement.signalPolarityValidated = config_.frontEnd.signalPolarityValidated;
    measurement.calibrationValidated = false;
    measurement.sensorCurrentA = NAN;
    measurement.zeroCorrectedCurrentA = NAN;
    measurement.concentrationPpm = NAN;
    measurement.valid = true;
    measurement.errorFlags = 0;
    measurement.monotonicTimestamp = std::chrono::steady_clock::now();
    measurement.wallTimestamp = std::chrono::system_clock::now();
    return {true, 0};
}

}  // namespace odor
