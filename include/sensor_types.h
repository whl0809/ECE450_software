#pragma once

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "config.h"
#include "error_flags.h"

namespace odor {

constexpr size_t TgsChannelCount = config::TgsChannelCount;
constexpr int32_t RawAdcUnavailable = std::numeric_limits<int32_t>::min();

enum class SystemState : uint8_t {
    Starting,
    Running,
    Stopping,
    Degraded,
    ConfigurationMissing,
};

struct SensorTimestamps {
    std::chrono::steady_clock::time_point tgs{};
    std::chrono::steady_clock::time_point nh3{};
    std::chrono::steady_clock::time_point h2s{};
    std::chrono::steady_clock::time_point sgp41{};
    std::chrono::steady_clock::time_point bme690{};
    std::chrono::steady_clock::time_point sht45{};
};

struct DriverStatus {
    bool configured = false;
    bool detected = false;
    ErrorFlags errorFlags = toErrorFlags(ErrorFlag::DeviceNotConfigured);
};

struct OperationResult {
    bool ok = false;
    ErrorFlags errorFlags = toErrorFlags(ErrorFlag::DeviceNotConfigured);
};

struct TgsArrayMeasurement {
    std::chrono::steady_clock::time_point monotonicTimestamp{};
    std::chrono::system_clock::time_point wallTimestamp{};
    std::array<int32_t, TgsChannelCount> adcRaw = {
        RawAdcUnavailable,
        RawAdcUnavailable,
        RawAdcUnavailable,
        RawAdcUnavailable,
        RawAdcUnavailable,
        RawAdcUnavailable,
    };
    std::array<config::TgsChannelConfig, TgsChannelCount> channelMap = config::TgsChannels;
    std::array<float, TgsChannelCount> voltageV = {NAN, NAN, NAN, NAN, NAN, NAN};
    std::array<float, TgsChannelCount> resistanceOhm = {NAN, NAN, NAN, NAN, NAN, NAN};
    std::array<float, TgsChannelCount> normalized = {NAN, NAN, NAN, NAN, NAN, NAN};
    std::array<bool, TgsChannelCount> channelFresh = {false, false, false, false, false, false};
    bool valid = false;
    ErrorFlags errorFlags = toErrorFlags(ErrorFlag::DeviceNotConfigured);
};

struct ElectrochemicalMeasurement {
    std::chrono::steady_clock::time_point monotonicTimestamp{};
    std::chrono::system_clock::time_point wallTimestamp{};
    int32_t adcRaw = RawAdcUnavailable;
    float frontEndVoltageV = NAN;
    float differentialVoltageV = NAN;
    float sensorCurrentA = NAN;
    float zeroCorrectedCurrentA = NAN;
    float concentrationPpm = NAN;
    bool signalPolarityValidated = false;
    bool calibrationValidated = false;
    bool valid = false;
    ErrorFlags errorFlags = toErrorFlags(ErrorFlag::DeviceNotConfigured);
};

struct Sgp41Measurement {
    std::chrono::steady_clock::time_point monotonicTimestamp{};
    std::chrono::system_clock::time_point wallTimestamp{};
    uint16_t rawVoc = 0;
    uint16_t rawNox = 0;
    int32_t vocIndex = std::numeric_limits<int32_t>::min();
    int32_t noxIndex = std::numeric_limits<int32_t>::min();
    bool rawValid = false;
    bool indexValid = false;
    bool compensationUsed = false;
    bool compensationStale = false;
    ErrorFlags errorFlags = toErrorFlags(ErrorFlag::DeviceNotConfigured);
};

struct Bme690Measurement {
    std::chrono::steady_clock::time_point monotonicTimestamp{};
    std::chrono::system_clock::time_point wallTimestamp{};
    float temperatureC = NAN;
    float humidityRh = NAN;
    float pressurePa = NAN;
    float gasResistanceOhm = NAN;
    uint32_t measurementStatus = 0;
    bool gasValid = false;
    bool heaterStable = false;
    bool valid = false;
    ErrorFlags errorFlags = toErrorFlags(ErrorFlag::DeviceNotConfigured);
};

struct Sht45Measurement {
    std::chrono::steady_clock::time_point monotonicTimestamp{};
    std::chrono::system_clock::time_point wallTimestamp{};
    float temperatureC = NAN;
    float humidityRh = NAN;
    bool valid = false;
    ErrorFlags errorFlags = toErrorFlags(ErrorFlag::DeviceNotConfigured);
};

struct SensorFrame {
    uint32_t schemaVersion = config::RawSchemaVersion;
    std::chrono::steady_clock::time_point monotonicTimestamp{};
    std::chrono::system_clock::time_point wallTimestamp{};
    SystemState systemState = SystemState::Starting;
    TgsArrayMeasurement tgs;
    ElectrochemicalMeasurement nh3;
    ElectrochemicalMeasurement h2s;
    Sgp41Measurement sgp41;
    Bme690Measurement bme690;
    Sht45Measurement sht45;
    SensorTimestamps sensorTimestamps{};
    uint64_t validFlags = 0;
    uint64_t errorFlags = 0;
};

}  // namespace odor
