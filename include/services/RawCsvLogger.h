#pragma once

#include <ostream>

#include "sensor_types.h"

namespace odor {

class RawCsvLogger {
public:
    static constexpr uint32_t SchemaVersion = config::RawSchemaVersion;

    static void writeHeader(std::ostream& out);
    static void writeFrame(std::ostream& out, const SensorFrame& frame);
};

}  // namespace odor
