#include "drivers/SGP41Driver.h"

#include "error_flags.h"
#include "protocol/ProtocolUtils.h"

#include <vector>

namespace odor {

namespace {

constexpr uint16_t Sgp41MeasureRawSignalsCommand = 0x2619;
constexpr uint16_t DefaultHumidityTicks = 0x8000;
constexpr uint16_t DefaultTemperatureTicks = 0x6666;

}  // namespace

SGP41Driver::SGP41Driver(hardware::II2CBus& bus, const SGP41Config& config)
    : bus_(bus), config_(config)
{
}

OperationResult SGP41Driver::begin()
{
    if (!config_.addressConfigured || !config_.busAssignmentConfirmed || !bus_.isConfigured()) {
        status_ = {false, false, toErrorFlags(ErrorFlag::DeviceNotConfigured)};
        return {false, status_.errorFlags};
    }

    status_ = {true, true, 0};
    return {true, 0};
}

DriverStatus SGP41Driver::status() const
{
    return status_;
}

void SGP41Driver::setCompensation(float temperatureC,
                                  float humidityRh,
                                  std::chrono::steady_clock::time_point timestamp)
{
    compensationTemperatureC_ = temperatureC;
    compensationHumidityRh_ = humidityRh;
    compensationTimestamp_ = timestamp;
    hasCompensation_ = !std::isnan(temperatureC) && !std::isnan(humidityRh);
}

OperationResult SGP41Driver::readRawSignals(Sgp41Measurement& measurement)
{
    if (!status_.configured || !status_.detected || status_.errorFlags != 0) {
        measurement.rawValid = false;
        measurement.indexValid = false;
        measurement.errorFlags = status_.errorFlags | toErrorFlags(ErrorFlag::DeviceNotConfigured);
        return {false, measurement.errorFlags};
    }

    const auto now = std::chrono::steady_clock::now();
    bool compensationFresh = false;
    if (hasCompensation_) {
        compensationFresh = (now - compensationTimestamp_) <= config_.runtime.compensationMaxAge;
    }

    uint16_t humidityTicks = DefaultHumidityTicks;
    uint16_t temperatureTicks = DefaultTemperatureTicks;
    if (config_.runtime.useSht45Compensation && compensationFresh) {
        humidityTicks = protocol::shtCompensationHumidityTicks(compensationHumidityRh_);
        temperatureTicks = protocol::shtCompensationTemperatureTicks(compensationTemperatureC_);
        measurement.compensationUsed = true;
    } else if (config_.runtime.useSht45Compensation) {
        measurement.compensationStale = true;
    }

    std::vector<uint8_t> command = {
        static_cast<uint8_t>((Sgp41MeasureRawSignalsCommand >> 8U) & 0xFFU),
        static_cast<uint8_t>(Sgp41MeasureRawSignalsCommand & 0xFFU),
    };
    protocol::appendU16WithSensirionCrc(command, humidityTicks);
    protocol::appendU16WithSensirionCrc(command, temperatureTicks);

    std::vector<uint8_t> response(6);
    const hardware::HardwareResult busResult = bus_.writeRead(config_.i2cAddress, command, response);
    if (!busResult.ok) {
        measurement.rawValid = false;
        measurement.errorFlags = toErrorFlags(ErrorFlag::I2cFailure) |
                                 toErrorFlags(ErrorFlag::CommunicationFailure);
        return {false, measurement.errorFlags};
    }
    if (response.size() != 6U) {
        measurement.rawValid = false;
        measurement.errorFlags = toErrorFlags(ErrorFlag::InvalidMeasurement);
        return {false, measurement.errorFlags};
    }
    if (!protocol::sensirionCrc8Matches(response.data(), 2, response[2]) ||
        !protocol::sensirionCrc8Matches(response.data() + 3, 2, response[5])) {
        measurement.rawValid = false;
        measurement.errorFlags = toErrorFlags(ErrorFlag::CrcFailure);
        return {false, measurement.errorFlags};
    }

    measurement.rawVoc = protocol::readU16Be(response, 0);
    measurement.rawNox = protocol::readU16Be(response, 3);
    measurement.rawValid = true;
    measurement.indexValid = false;
    measurement.errorFlags = measurement.compensationStale ? toErrorFlags(ErrorFlag::StaleMeasurement) : 0;
    measurement.monotonicTimestamp = now;
    measurement.wallTimestamp = std::chrono::system_clock::now();
    return {true, measurement.errorFlags};
}

}  // namespace odor
