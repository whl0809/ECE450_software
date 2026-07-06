#pragma once

#include <cstdint>

namespace odor {

enum class ErrorFlag : uint32_t {
    None = 0,
    DeviceNotConfigured = 1UL << 0,
    DeviceNotDetected = 1UL << 1,
    I2cFailure = 1UL << 2,
    SpiFailure = 1UL << 3,
    CrcFailure = 1UL << 4,
    Timeout = 1UL << 5,
    AdcSaturation = 1UL << 6,
    InvalidMeasurement = 1UL << 7,
    MissingCalibration = 1UL << 8,
    StaleMeasurement = 1UL << 9,
    NotImplemented = 1UL << 10,
    InvalidConfiguration = 1UL << 11,
    NotReady = 1UL << 12,
    CommunicationFailure = 1UL << 13,
    ConditioningIncomplete = 1UL << 14,
    HeaterNotStable = 1UL << 15,
};

using ErrorFlags = uint64_t;

constexpr ErrorFlags toErrorFlags(ErrorFlag flag)
{
    return static_cast<ErrorFlags>(flag);
}

constexpr bool hasError(ErrorFlags flags, ErrorFlag flag)
{
    return (flags & toErrorFlags(flag)) != 0U;
}

constexpr void setError(ErrorFlags& flags, ErrorFlag flag)
{
    flags |= toErrorFlags(flag);
}

constexpr void clearError(ErrorFlags& flags, ErrorFlag flag)
{
    flags &= ~toErrorFlags(flag);
}

enum class SensorGroup : uint32_t {
    TgsArray = 1UL << 0,
    Nh3 = 1UL << 1,
    H2s = 1UL << 2,
    Sgp41 = 1UL << 3,
    Bme690 = 1UL << 4,
    Sht45 = 1UL << 5,
};

using ValidFlags = uint64_t;

constexpr ValidFlags toValidFlags(SensorGroup group)
{
    return static_cast<ValidFlags>(group);
}

constexpr bool hasValidFlag(ValidFlags flags, SensorGroup group)
{
    return (flags & toValidFlags(group)) != 0U;
}

constexpr void setValidFlag(ValidFlags& flags, SensorGroup group)
{
    flags |= toValidFlags(group);
}

constexpr void clearValidFlag(ValidFlags& flags, SensorGroup group)
{
    flags &= ~toValidFlags(group);
}

}  // namespace odor
