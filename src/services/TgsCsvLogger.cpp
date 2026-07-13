#include "services/TgsCsvLogger.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace odor {

namespace {

std::tm utcTime(std::chrono::system_clock::time_point value)
{
    const std::time_t time = std::chrono::system_clock::to_time_t(value);
    std::tm result{};
#if defined(_WIN32)
    gmtime_s(&result, &time);
#else
    gmtime_r(&time, &result);
#endif
    return result;
}

std::string utcTimestamp(std::chrono::system_clock::time_point value)
{
    std::ostringstream out;
    const std::tm utc = utcTime(value);
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

uint64_t elapsedMs(std::chrono::steady_clock::time_point start,
                   std::chrono::steady_clock::time_point value)
{
    if (value < start) {
        return 0;
    }
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(value - start).count());
}

void writeFloat(std::ostream& out, float value)
{
    if (std::isnan(value)) {
        out << "nan";
    } else {
        out << std::setprecision(8) << value;
    }
}

}  // namespace

void TgsCsvLogger::writeHeader(std::ostream& out)
{
    out << "timestamp_utc,elapsed_ms";
    for (size_t i = 0; i < config::TgsChannelCount; ++i) {
        out << ",tgs" << i << "_raw,tgs" << i << "_voltage_v";
    }
    out << ",error_flags\n";
}

void TgsCsvLogger::writeScan(std::ostream& out,
                             const TgsArrayMeasurement& measurement,
                             std::chrono::steady_clock::time_point recordingStart)
{
    out << utcTimestamp(measurement.wallTimestamp) << ','
        << elapsedMs(recordingStart, measurement.monotonicTimestamp);
    for (size_t i = 0; i < config::TgsChannelCount; ++i) {
        out << ',';
        if (measurement.channelFresh[i]) {
            out << measurement.adcRaw[i];
        }
        out << ',';
        if (measurement.channelFresh[i]) {
            writeFloat(out, measurement.voltageV[i]);
        }
    }
    out << ',' << measurement.errorFlags << '\n';
}

std::string TgsCsvLogger::timestampedFilename(std::chrono::system_clock::time_point now)
{
    std::ostringstream out;
    const std::tm utc = utcTime(now);
    out << "tgs_timeseries_" << std::put_time(&utc, "%Y%m%d_%H%M%SZ") << ".csv";
    return out.str();
}

}  // namespace odor
