#include "drivers/SHT45Driver.h"

#include "error_flags.h"
#include "protocol/ProtocolUtils.h"

#include <vector>

namespace odor {

namespace {

constexpr uint8_t Sht45MeasureHighPrecisionNoHeater = 0xFD;
constexpr uint8_t Sht45MeasureMediumPrecisionNoHeater = 0xF6;
constexpr uint8_t Sht45MeasureLowPrecisionNoHeater = 0xE0;

uint8_t commandFor(config::Sht45Precision precision)
{
    switch (precision) {
    case config::Sht45Precision::High:
        return Sht45MeasureHighPrecisionNoHeater;
    case config::Sht45Precision::Medium:
        return Sht45MeasureMediumPrecisionNoHeater;
    case config::Sht45Precision::Low:
        return Sht45MeasureLowPrecisionNoHeater;
    }
    return Sht45MeasureHighPrecisionNoHeater;
}

}  // namespace

SHT45Driver::SHT45Driver(hardware::II2CBus& bus, const SHT45Config& config)
    : bus_(bus), config_(config)
{
}

OperationResult SHT45Driver::begin()
{
    if (!config_.addressConfigured || !config_.busAssignmentConfirmed || !bus_.isConfigured()) {
        status_ = {false, false, toErrorFlags(ErrorFlag::DeviceNotConfigured)};
        return {false, status_.errorFlags};
    }

    if (config_.runtime.heaterEnabled) {
        status_ = {true, false, toErrorFlags(ErrorFlag::InvalidConfiguration)};
        return {false, status_.errorFlags};
    }

    status_ = {true, true, 0};
    return {true, 0};
}

DriverStatus SHT45Driver::status() const
{
    return status_;
}

OperationResult SHT45Driver::readMeasurement(Sht45Measurement& measurement)
{
    if (!status_.configured || !status_.detected || status_.errorFlags != 0) {
        measurement.valid = false;
        measurement.errorFlags = status_.errorFlags | toErrorFlags(ErrorFlag::DeviceNotConfigured);
        return {false, measurement.errorFlags};
    }

    std::vector<uint8_t> command = {commandFor(config_.runtime.precision)};
    std::vector<uint8_t> response(6);
    const hardware::HardwareResult busResult = bus_.writeRead(config_.i2cAddress, command, response);
    if (!busResult.ok) {
        measurement.valid = false;
        measurement.errorFlags = toErrorFlags(ErrorFlag::I2cFailure) |
                                 toErrorFlags(ErrorFlag::CommunicationFailure);
        return {false, measurement.errorFlags};
    }
    if (response.size() != 6U) {
        measurement.valid = false;
        measurement.errorFlags = toErrorFlags(ErrorFlag::InvalidMeasurement);
        return {false, measurement.errorFlags};
    }

    if (config_.runtime.crcEnabled &&
        (!protocol::sensirionCrc8Matches(response.data(), 2, response[2]) ||
         !protocol::sensirionCrc8Matches(response.data() + 3, 2, response[5]))) {
        measurement.valid = false;
        measurement.errorFlags = toErrorFlags(ErrorFlag::CrcFailure);
        return {false, measurement.errorFlags};
    }

    const uint16_t rawTemperature = protocol::readU16Be(response, 0);
    const uint16_t rawHumidity = protocol::readU16Be(response, 3);

    measurement.temperatureC = -45.0F + (175.0F * static_cast<float>(rawTemperature) / 65535.0F);
    measurement.humidityRh = -6.0F + (125.0F * static_cast<float>(rawHumidity) / 65535.0F);
    measurement.valid = true;
    measurement.errorFlags = 0;
    measurement.monotonicTimestamp = std::chrono::steady_clock::now();
    measurement.wallTimestamp = std::chrono::system_clock::now();
    return {true, 0};
}

}  // namespace odor
