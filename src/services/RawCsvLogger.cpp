#include "services/RawCsvLogger.h"

#include <chrono>
#include <cmath>
#include <iomanip>

namespace odor {

namespace {

template <typename T>
void writeValue(std::ostream& out, T value)
{
    out << value;
}

void writeFloat(std::ostream& out, float value)
{
    if (std::isnan(value)) {
        out << "nan";
    } else {
        out << std::setprecision(8) << value;
    }
}

uint64_t steadyMs(std::chrono::steady_clock::time_point value)
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count());
}

int64_t wallMs(std::chrono::system_clock::time_point value)
{
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count());
}

}  // namespace

void RawCsvLogger::writeHeader(std::ostream& out)
{
    out << "schema_version,monotonic_ms,wall_unix_ms,system_state,valid_flags,error_flags";
    for (const auto& channel : config::TgsChannels) {
        out << ',' << channel.signalName << "_raw";
        out << ',' << channel.signalName << "_voltage_v";
    }
    out << ",nh3_raw,nh3_diff_v,h2s_raw,h2s_diff_v";
    out << ",sgp41_sraw_voc,sgp41_sraw_nox";
    out << ",bme690_temp_c,bme690_humidity_rh,bme690_pressure_pa,bme690_gas_ohm";
    out << ",bme690_gas_valid,bme690_heater_stable";
    out << ",sht45_temp_c,sht45_humidity_rh";
    out << '\n';
}

void RawCsvLogger::writeFrame(std::ostream& out, const SensorFrame& frame)
{
    out << frame.schemaVersion << ','
        << steadyMs(frame.monotonicTimestamp) << ','
        << wallMs(frame.wallTimestamp) << ','
        << static_cast<int>(frame.systemState) << ','
        << frame.validFlags << ','
        << frame.errorFlags;

    for (size_t i = 0; i < config::TgsChannelCount; ++i) {
        out << ',';
        writeValue(out, frame.tgs.adcRaw[i]);
        out << ',';
        writeFloat(out, frame.tgs.voltageV[i]);
    }

    out << ',' << frame.nh3.adcRaw << ',';
    writeFloat(out, frame.nh3.differentialVoltageV);
    out << ',' << frame.h2s.adcRaw << ',';
    writeFloat(out, frame.h2s.differentialVoltageV);
    out << ',' << frame.sgp41.rawVoc << ',' << frame.sgp41.rawNox << ',';
    writeFloat(out, frame.bme690.temperatureC);
    out << ',';
    writeFloat(out, frame.bme690.humidityRh);
    out << ',';
    writeFloat(out, frame.bme690.pressurePa);
    out << ',';
    writeFloat(out, frame.bme690.gasResistanceOhm);
    out << ',' << (frame.bme690.gasValid ? 1 : 0);
    out << ',' << (frame.bme690.heaterStable ? 1 : 0);
    out << ',';
    writeFloat(out, frame.sht45.temperatureC);
    out << ',';
    writeFloat(out, frame.sht45.humidityRh);
    out << '\n';
}

}  // namespace odor
