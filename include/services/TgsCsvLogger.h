#pragma once

#include <chrono>
#include <ostream>
#include <string>

#include "sensor_types.h"

namespace odor {

class TgsCsvLogger {
public:
    static void writeHeader(std::ostream& out);
    static void writeScan(std::ostream& out,
                          const TgsArrayMeasurement& measurement,
                          std::chrono::steady_clock::time_point recordingStart);
    static std::string timestampedFilename(std::chrono::system_clock::time_point now);
};

}  // namespace odor
