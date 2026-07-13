#include <cmath>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>

#include "drivers/ADS114S06Driver.h"
#include "drivers/BME690Driver.h"
#include "drivers/MCP3421Driver.h"
#include "drivers/SGP41Driver.h"
#include "drivers/SHT45Driver.h"
#include "hardware/HardwareResult.h"
#include "hardware/mock/MockGpioLine.h"
#include "hardware/mock/MockI2CBus.h"
#include "hardware/mock/MockSPIDevice.h"
#include "protocol/ProtocolUtils.h"
#include "services/RawCsvLogger.h"

namespace {

int failures = 0;

void expect(bool condition)
{
    if (!condition) {
        ++failures;
    }
}

std::vector<uint8_t> sensirionWordPair(uint16_t first, uint16_t second)
{
    std::vector<uint8_t> bytes;
    odor::protocol::appendU16WithSensirionCrc(bytes, first);
    odor::protocol::appendU16WithSensirionCrc(bytes, second);
    return bytes;
}

void setBmeCoeff16(std::vector<uint8_t>& coeff, size_t msbIndex, size_t lsbIndex, uint16_t value)
{
    coeff[msbIndex] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    coeff[lsbIndex] = static_cast<uint8_t>(value & 0xFFU);
}

std::vector<uint8_t> bmeCoeffAll()
{
    std::vector<uint8_t> coeff(42, 0);
    setBmeCoeff16(coeff, 1, 0, 65535);     // par_t2
    setBmeCoeff16(coeff, 5, 4, 16384);     // par_p5 makes pressure sensitivity near zero
    setBmeCoeff16(coeff, 11, 10, 10000);   // par_p1 -> pressure near 80 kPa
    coeff[23] = 64;                        // par_h5 = 1024
    coeff[24] = 0;
    coeff[35] = 0;                         // par_g1
    setBmeCoeff16(coeff, 34, 33, 0);       // par_g2
    coeff[36] = 0;                         // par_g3
    coeff[37] = 0;                         // res_heat_val
    coeff[39] = 0;                         // res_heat_range
    return coeff;
}

void queueBmeBegin(odor::hardware::mock::MockI2CBus& bus)
{
    const std::vector<uint8_t> coeff = bmeCoeffAll();
    bus.queueReadData({0x61});  // chip ID
    bus.queueReadData({0x00});  // variant ID
    bus.queueReadData(std::vector<uint8_t>(coeff.begin(), coeff.begin() + 23));
    bus.queueReadData(std::vector<uint8_t>(coeff.begin() + 23, coeff.begin() + 37));
    bus.queueReadData(std::vector<uint8_t>(coeff.begin() + 37, coeff.end()));
    bus.queueReadData({0x00});  // CTRL_MEAS read before initial sleep/config
}

std::vector<uint8_t> bmeField(bool fresh = true, bool gasValid = true, bool heaterStable = true)
{
    std::vector<uint8_t> field(17, 0);
    field[0] = fresh ? 0x80 : 0x00;
    field[2] = 0x01;
    field[3] = 0x86;
    field[4] = 0xA0;
    field[5] = 0x06;
    field[6] = 0x40;
    field[7] = 0x00;
    field[8] = 0x0C;
    field[9] = 0x80;
    field[15] = 0x80;
    field[16] = static_cast<uint8_t>((gasValid ? 0x20 : 0x00) |
                                     (heaterStable ? 0x10 : 0x00));
    return field;
}

void queueBmeMeasurement(odor::hardware::mock::MockI2CBus& bus,
                         const std::vector<uint8_t>& field)
{
    bus.queueReadData({0x00});  // CTRL_MEAS read before per-sample config
    bus.queueReadData({0x00});  // CTRL_MEAS read before forcing mode
    const bool fresh = !field.empty() && ((field[0] & 0x80U) != 0U);
    const int fieldReads = fresh ? 1 : 5;
    for (int i = 0; i < fieldReads; ++i) {
        bus.queueReadData(field);
    }
}

void queueAdsRegisterCheck(odor::hardware::mock::MockSPIDevice& spi, uint8_t value)
{
    spi.queueRxData({});
    spi.queueRxData({0x00, 0x00, value});
}

void queueAdsBeginWithId(odor::hardware::mock::MockSPIDevice& spi, uint8_t deviceIdRegister)
{
    spi.queueRxData({0x00, 0x00, deviceIdRegister});
    queueAdsRegisterCheck(spi, 0x00);
    queueAdsRegisterCheck(spi, 0x14);
    queueAdsRegisterCheck(spi, 0x3A);
}

void queueAdsBegin(odor::hardware::mock::MockSPIDevice& spi)
{
    queueAdsBeginWithId(spi, 0x05);  // ADS114S06 DEV_ID is in ID register bits 2:0.
}

void queueAdsChannelRead(odor::hardware::mock::MockSPIDevice& spi, uint8_t muxValue, int16_t sample)
{
    queueAdsRegisterCheck(spi, muxValue);
    spi.queueRxData({});
    spi.queueRxData({0x00,
                     static_cast<uint8_t>((static_cast<uint16_t>(sample) >> 8U) & 0xFFU),
                     static_cast<uint8_t>(static_cast<uint16_t>(sample) & 0xFFU)});
}

}  // namespace

int main()
{
    {
        odor::hardware::mock::MockI2CBus bus(true);
        bus.setReadData(sensirionWordPair(0x8000, 0x8000));
        odor::SHT45Driver driver(bus, {true, odor::config::Sht45I2cAddress, true, odor::config::Sht45Defaults});
        expect(driver.begin().ok);
        odor::Sht45Measurement measurement;
        expect(driver.readMeasurement(measurement).ok);
        expect(measurement.valid);
        expect(measurement.temperatureC > 42.0F && measurement.temperatureC < 43.0F);
        expect(measurement.humidityRh > 56.0F && measurement.humidityRh < 57.0F);
    }

    {
        odor::hardware::mock::MockI2CBus bus(true);
        std::vector<uint8_t> bad = sensirionWordPair(0x8000, 0x8000);
        bad[2] ^= 0xFFU;
        bus.setReadData(bad);
        odor::SHT45Driver driver(bus, {true, odor::config::Sht45I2cAddress, true, odor::config::Sht45Defaults});
        expect(driver.begin().ok);
        odor::Sht45Measurement measurement;
        expect(!driver.readMeasurement(measurement).ok);
        expect(odor::hasError(measurement.errorFlags, odor::ErrorFlag::CrcFailure));
    }

    {
        odor::hardware::mock::MockI2CBus bus(true);
        bus.failNextTransfer(-5, "forced I2C failure");
        odor::SHT45Driver driver(bus, {true, odor::config::Sht45I2cAddress, true, odor::config::Sht45Defaults});
        expect(driver.begin().ok);
        odor::Sht45Measurement measurement;
        expect(!driver.readMeasurement(measurement).ok);
        expect(odor::hasError(measurement.errorFlags, odor::ErrorFlag::I2cFailure));
    }

    {
        odor::hardware::mock::MockI2CBus bus(true);
        bus.setReadData(sensirionWordPair(0x1234, 0x5678));
        odor::SGP41Driver driver(bus, {true, odor::config::Sgp41I2cAddress, true, odor::config::Sgp41Defaults});
        expect(driver.begin().ok);
        driver.setCompensation(25.0F, 50.0F, std::chrono::steady_clock::now());
        odor::Sgp41Measurement measurement;
        expect(driver.readRawSignals(measurement).ok);
        expect(measurement.rawValid);
        expect(measurement.rawVoc == 0x1234);
        expect(measurement.rawNox == 0x5678);
        expect(measurement.compensationUsed);
    }

    {
        odor::hardware::mock::MockI2CBus bus(true);
        bus.setReadData({0xFF, 0x00, 0x00});
        odor::MCP3421Driver driver(
            bus,
            {true, odor::config::Nh3Mcp3421I2cAddress, true, "NH3", odor::config::Nh3FrontEnd, odor::config::Mcp3421Defaults});
        expect(driver.begin().ok);
        odor::ElectrochemicalMeasurement measurement;
        expect(driver.readElectrochemical(measurement).ok);
        expect(measurement.adcRaw == -256);
        expect(measurement.differentialVoltageV < 0.0F);
        expect(std::isnan(measurement.concentrationPpm));
    }

    {
        odor::hardware::mock::MockI2CBus bus(true);
        bus.setReadData({0x00, 0x00, 0x80});
        odor::MCP3421Driver driver(
            bus,
            {true, odor::config::Nh3Mcp3421I2cAddress, true, "NH3", odor::config::Nh3FrontEnd, odor::config::Mcp3421Defaults});
        expect(driver.begin().ok);
        odor::ElectrochemicalMeasurement measurement;
        expect(!driver.readElectrochemical(measurement).ok);
        expect(odor::hasError(measurement.errorFlags, odor::ErrorFlag::NotReady));
    }

    {
        odor::hardware::mock::MockSPIDevice spi(true);
        spi.queueRxData({});
        spi.queueRxData({0x00, 0x00, 0xA5, 0x80, 0x01, 0x00});
        odor::hardware::mock::MockGpioLine start(true);
        odor::ADS114S06Driver driver(spi, nullptr, &start, {true, false, true, true, false, 2.5F, odor::config::Ads114s06Defaults});
        std::vector<odor::ADS114S06DiagnosticEvent> events;
        driver.setDiagnosticCallback([&events](const odor::ADS114S06DiagnosticEvent& event) {
            events.push_back(event);
        });
        const odor::OperationResult result = driver.runResetRegisterSnapshotDiagnostic();
        expect(result.ok);
        expect(events.size() == 2U);
        bool startValue = false;
        expect(start.read(startValue).ok);
        expect(!startValue);
        expect(!start.wasEverWrittenHigh());
        expect(start.writeCount() == 1U);
        expect(events[0].stage == "reset_command");
        expect(events[0].txBytes == std::vector<uint8_t>({0x06}));
        expect(events[1].stage == "reset_register_snapshot_rreg");
        expect(events[1].txBytes == std::vector<uint8_t>({0x20, 0x03, 0x00, 0x00, 0x00, 0x00}));
        expect(events[1].rxBytes == std::vector<uint8_t>({0x00, 0x00, 0xA5, 0x80, 0x01, 0x00}));
        expect(events[1].extractedRegisterBytes == std::vector<uint8_t>({0xA5, 0x80, 0x01, 0x00}));
        expect(events[1].hasComparison);
        expect(events[1].readbackMask == 0x07);
        expect(events[1].requestedWriteValue == 0x05);
        expect(events[1].extractedReadbackValue == 0xA5);
        expect(events[1].maskedExpected == 0x05);
        expect(events[1].maskedActual == 0x05);
    }

    {
        odor::hardware::mock::MockSPIDevice spi(true);
        spi.queueRxData({});
        spi.queueRxData({0x00, 0x00, 0xA5, 0x00, 0x00, 0x01});
        odor::ADS114S06Driver driver(spi, {true, false, false, true, false, 2.5F, odor::config::Ads114s06Defaults});
        std::vector<odor::ADS114S06DiagnosticEvent> events;
        driver.setDiagnosticCallback([&events](const odor::ADS114S06DiagnosticEvent& event) {
            events.push_back(event);
        });
        const odor::OperationResult result = driver.runResetRegisterSnapshotDiagnostic();
        expect(result.ok);
        expect(events.size() == 2U);
        expect(events[1].extractedRegisterBytes == std::vector<uint8_t>({0xA5, 0x00, 0x00, 0x01}));
        expect(events[1].maskedActual == 0x05);
        expect(events[1].errorFlags == 0U);
    }

    {
        odor::hardware::mock::MockSPIDevice spi(true);
        spi.queueRxData({});
        spi.queueRxData({0x00, 0x00, 0xFF, 0x80, 0x01, 0x00});
        odor::ADS114S06Driver driver(spi, {true, false, false, true, false, 2.5F, odor::config::Ads114s06Defaults});
        std::vector<odor::ADS114S06DiagnosticEvent> events;
        driver.setDiagnosticCallback([&events](const odor::ADS114S06DiagnosticEvent& event) {
            events.push_back(event);
        });
        const odor::OperationResult result = driver.runResetRegisterSnapshotDiagnostic();
        expect(!result.ok);
        expect(odor::hasError(result.errorFlags, odor::ErrorFlag::DeviceNotDetected));
        expect(events.size() == 2U);
        expect(events[1].extractedRegisterBytes == std::vector<uint8_t>({0xFF, 0x80, 0x01, 0x00}));
        expect(events[1].maskedExpected == 0x05);
        expect(events[1].maskedActual == 0x07);
    }

    {
        odor::hardware::mock::MockSPIDevice spi(true);
        queueAdsBegin(spi);
        for (size_t i = 0; i < odor::config::TgsChannelCount; ++i) {
            const uint8_t muxValue =
                static_cast<uint8_t>((odor::config::TgsChannels[i].ads114s06Ain << 4U) | 0x0CU);
            queueAdsChannelRead(spi, muxValue, 0x1234);
        }
        odor::hardware::mock::MockGpioLine start(true);
        odor::ADS114S06Driver driver(spi, nullptr, &start, {true, false, true, true, false, 2.5F, odor::config::Ads114s06Defaults});
        std::vector<odor::ADS114S06DiagnosticEvent> events;
        driver.setDiagnosticCallback([&events](const odor::ADS114S06DiagnosticEvent& event) {
            events.push_back(event);
        });
        const auto beginStarted = std::chrono::steady_clock::now();
        const odor::OperationResult beginResult = driver.begin();
        const auto beginElapsed = std::chrono::steady_clock::now() - beginStarted;
        expect(beginResult.ok);
        expect(beginElapsed >= std::chrono::milliseconds(10));
        expect(events.size() >= 11U);
        expect(events[0].stage == "device_id_read_rreg");
        expect(events[0].txBytes == std::vector<uint8_t>({0x20, 0x00, 0x00}));
        expect(!events[0].hasComparison);
        expect(events[0].extractedReadbackValue == 0x05);
        expect(events[1].stage == "device_id_verify");
        expect(events[1].hasComparison);
        expect(events[1].readbackMask == 0x07);
        expect(events[1].requestedWriteValue == 0x05);
        expect(events[1].maskedExpected == 0x05);
        expect(events[1].maskedActual == 0x05);
        expect(events[2].txBytes == std::vector<uint8_t>({0x43, 0x00, 0x00}));
        expect(!events[2].hasComparison);
        expect(events[3].txBytes == std::vector<uint8_t>({0x23, 0x00, 0x00}));
        expect(!events[3].hasComparison);
        expect(events[4].maskedExpected == events[4].maskedActual);
        bool sawRefWrite = false;
        bool sawRefRead = false;
        bool sawRefCompare = false;
        for (const auto& event : events) {
            sawRefWrite = sawRefWrite || event.txBytes == std::vector<uint8_t>({0x45, 0x00, 0x3A});
            sawRefRead = sawRefRead || event.txBytes == std::vector<uint8_t>({0x25, 0x00, 0x00});
            if (event.stage == "register_0x5_masked_compare") {
                sawRefCompare = event.readbackMask == 0xFF &&
                                event.requestedWriteValue == 0x3A &&
                                event.extractedReadbackValue == 0x3A &&
                                event.maskedExpected == 0x3A &&
                                event.maskedActual == 0x3A;
            }
        }
        expect(sawRefWrite);
        expect(sawRefRead);
        expect(sawRefCompare);
        odor::TgsArrayMeasurement measurement;
        expect(driver.readTgsArray(measurement).ok);
        bool startValue = true;
        expect(start.read(startValue).ok);
        expect(!startValue);
        expect(!start.wasEverWrittenHigh());
        expect(start.writeCount() >= 7U);
        bool sawSerialStart = false;
        for (const auto& tx : spi.txHistory()) {
            sawSerialStart = sawSerialStart || tx == std::vector<uint8_t>({0x08});
        }
        expect(sawSerialStart);
        expect(measurement.valid);
        expect(measurement.adcRaw[0] == 0x1234);
        expect(measurement.voltageV[0] > 0.35F && measurement.voltageV[0] < 0.36F);
    }

    {
        odor::hardware::mock::MockSPIDevice spi(true);
        spi.queueRxData({0x00, 0x00, 0xFF});
        odor::ADS114S06Driver driver(spi, {true, false, false, true, false, 2.5F, odor::config::Ads114s06Defaults});
        std::vector<odor::ADS114S06DiagnosticEvent> events;
        driver.setDiagnosticCallback([&events](const odor::ADS114S06DiagnosticEvent& event) {
            events.push_back(event);
        });
        expect(!driver.begin().ok);
        expect(odor::hasError(driver.status().errorFlags, odor::ErrorFlag::DeviceNotDetected));
        expect(events.size() == 2U);
        expect(events.back().stage == "device_id_verify");
        expect(events.back().hasComparison);
        expect(events.back().readbackMask == 0x07);
        expect(events.back().requestedWriteValue == 0x05);
        expect(events.back().extractedReadbackValue == 0xFF);
        expect(events.back().maskedExpected == 0x05);
        expect(events.back().maskedActual == 0x07);
    }

    {
        const std::vector<uint8_t> idsWithMatchingLowBits = {0x05, 0x25, 0xA5, 0xFD};
        for (uint8_t idRegister : idsWithMatchingLowBits) {
            odor::hardware::mock::MockSPIDevice spi(true);
            queueAdsBeginWithId(spi, idRegister);
            odor::ADS114S06Driver driver(spi, {true, false, false, true, false, 2.5F, odor::config::Ads114s06Defaults});
            std::vector<odor::ADS114S06DiagnosticEvent> events;
            driver.setDiagnosticCallback([&events](const odor::ADS114S06DiagnosticEvent& event) {
                events.push_back(event);
            });
            expect(driver.begin().ok);
            expect(events.size() >= 2U);
            expect(events[1].stage == "device_id_verify");
            expect(events[1].hasComparison);
            expect(events[1].readbackMask == 0x07);
            expect(events[1].requestedWriteValue == 0x05);
            expect(events[1].extractedReadbackValue == idRegister);
            expect(events[1].maskedExpected == 0x05);
            expect(events[1].maskedActual == 0x05);
        }
    }

    {
        odor::hardware::mock::MockSPIDevice spi(true);
        spi.queueRxData({0x00, 0x00, 0x05});
        queueAdsRegisterCheck(spi, 0x01);
        odor::ADS114S06Driver driver(spi, {true, false, false, true, false, 2.5F, odor::config::Ads114s06Defaults});
        std::vector<odor::ADS114S06DiagnosticEvent> events;
        driver.setDiagnosticCallback([&events](const odor::ADS114S06DiagnosticEvent& event) {
            events.push_back(event);
        });
        expect(!driver.begin().ok);
        expect(odor::hasError(driver.status().errorFlags, odor::ErrorFlag::InvalidConfiguration));
        bool sawPgaMismatch = false;
        bool sawDataRateStage = false;
        for (const auto& event : events) {
            sawPgaMismatch = sawPgaMismatch || event.stage == "register_0x3_masked_compare";
            sawDataRateStage = sawDataRateStage || event.stage.find("register_0x4") != std::string::npos;
        }
        expect(sawPgaMismatch);
        expect(!sawDataRateStage);
        expect(events.back().readbackMask == 0xFF);
        expect(events.back().maskedExpected == 0x00);
        expect(events.back().maskedActual == 0x01);
    }

    {
        odor::hardware::mock::MockSPIDevice spi(true);
        odor::hardware::mock::MockGpioLine drdy(true);
        queueAdsBegin(spi);
        for (size_t i = 0; i < odor::config::TgsChannelCount; ++i) {
            const uint8_t muxValue =
                static_cast<uint8_t>((odor::config::TgsChannels[i].ads114s06Ain << 4U) | 0x0CU);
            queueAdsRegisterCheck(spi, muxValue);
            spi.queueRxData({});
        }
        drdy.setPersistentWaitResult(odor::hardware::HardwareResult::failure(-110, "timeout"));
        odor::ADS114S06Driver driver(spi, &drdy, {true, true, false, true, false, 2.5F, odor::config::Ads114s06Defaults});
        expect(driver.begin().ok);
        odor::TgsArrayMeasurement measurement;
        expect(!driver.readTgsArray(measurement).ok);
        expect(odor::hasError(measurement.errorFlags, odor::ErrorFlag::Timeout));
        expect(odor::hasError(measurement.errorFlags, odor::ErrorFlag::NotReady));
    }

    {
        odor::hardware::mock::MockSPIDevice spi(true);
        queueAdsBegin(spi);
        for (size_t i = 0; i < odor::config::TgsChannelCount; ++i) {
            const uint8_t muxValue =
                static_cast<uint8_t>((odor::config::TgsChannels[i].ads114s06Ain << 4U) | 0x0CU);
            queueAdsChannelRead(spi, muxValue, -256);
        }
        odor::ADS114S06Driver driver(spi, {true, false, false, true, false, 2.5F, odor::config::Ads114s06Defaults});
        expect(driver.begin().ok);
        odor::TgsArrayMeasurement measurement;
        expect(driver.readTgsArray(measurement).ok);
        expect(measurement.adcRaw[0] == -256);
        expect(measurement.voltageV[0] < 0.0F);
    }

    {
        odor::hardware::mock::MockI2CBus bus(true);
        queueBmeBegin(bus);
        queueBmeMeasurement(bus, bmeField());
        odor::BME690Driver driver(bus, {true, odor::config::Bme690I2cAddress, true, odor::config::Bme690Defaults});
        expect(driver.begin().ok);
        odor::Bme690Measurement measurement;
        expect(driver.readMeasurement(measurement).ok);
        expect(measurement.gasValid);
        expect(measurement.heaterStable);
        expect(std::isfinite(measurement.temperatureC));
        expect(std::isfinite(measurement.humidityRh));
        expect(std::isfinite(measurement.pressurePa));
        expect(std::isfinite(measurement.gasResistanceOhm));
    }

    {
        odor::hardware::mock::MockI2CBus bus(true);
        bus.queueReadData({0x00});
        odor::BME690Driver driver(bus, {true, odor::config::Bme690I2cAddress, true, odor::config::Bme690Defaults});
        expect(!driver.begin().ok);
        expect(odor::hasError(driver.status().errorFlags, odor::ErrorFlag::DeviceNotDetected));
    }

    {
        odor::hardware::mock::MockI2CBus bus(true);
        queueBmeBegin(bus);
        queueBmeMeasurement(bus, bmeField(false));
        odor::BME690Driver driver(bus, {true, odor::config::Bme690I2cAddress, true, odor::config::Bme690Defaults});
        expect(driver.begin().ok);
        odor::Bme690Measurement measurement;
        expect(!driver.readMeasurement(measurement).ok);
        expect(odor::hasError(measurement.errorFlags, odor::ErrorFlag::NotReady));
        expect(odor::hasError(measurement.errorFlags, odor::ErrorFlag::Timeout));
    }

    {
        odor::hardware::mock::MockI2CBus bus(true);
        queueBmeBegin(bus);
        queueBmeMeasurement(bus, bmeField(true, false, false));
        odor::BME690Driver driver(bus, {true, odor::config::Bme690I2cAddress, true, odor::config::Bme690Defaults});
        expect(driver.begin().ok);
        odor::Bme690Measurement measurement;
        expect(!driver.readMeasurement(measurement).ok);
        expect(!measurement.gasValid);
        expect(!measurement.heaterStable);
        expect(odor::hasError(measurement.errorFlags, odor::ErrorFlag::HeaterNotStable));
    }

    {
        odor::hardware::mock::MockI2CBus bus(false);
        odor::SHT45Driver driver(bus, {true, odor::config::Sht45I2cAddress, true, odor::config::Sht45Defaults});
        expect(!driver.begin().ok);
    }

    {
        std::ostringstream out;
        odor::SensorFrame frame;
        frame.monotonicTimestamp = std::chrono::steady_clock::now();
        frame.wallTimestamp = std::chrono::system_clock::now();
        odor::RawCsvLogger::writeHeader(out);
        odor::RawCsvLogger::writeFrame(out, frame);
        const std::string csv = out.str();
        expect(csv.find("schema_version") != std::string::npos);
        expect(csv.find("TGS2610_VOUT_raw") != std::string::npos);
    }

    return failures;
}
