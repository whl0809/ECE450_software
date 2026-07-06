#include "drivers/BME690Driver.h"

#include "error_flags.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace odor {

namespace {

// Register names and compensation flow are adapted from Bosch Sensortec's
// official BME690 SensorAPI (bme69x v1.1.0).
constexpr uint8_t Bme690ChipId = 0x61;
constexpr uint8_t RegCoeff3 = 0x00;
constexpr uint8_t RegField0 = 0x1D;
constexpr uint8_t RegIdacHeat0 = 0x50;
constexpr uint8_t RegResHeat0 = 0x5A;
constexpr uint8_t RegGasWait0 = 0x64;
constexpr uint8_t RegCtrlGas0 = 0x70;
constexpr uint8_t RegCtrlGas1 = 0x71;
constexpr uint8_t RegCtrlHum = 0x72;
constexpr uint8_t RegCtrlMeas = 0x74;
constexpr uint8_t RegConfig = 0x75;
constexpr uint8_t RegCoeff1 = 0x8A;
constexpr uint8_t RegChipId = 0xD0;
constexpr uint8_t RegSoftReset = 0xE0;
constexpr uint8_t RegCoeff2 = 0xE1;
constexpr uint8_t RegVariantId = 0xF0;

constexpr uint8_t SoftResetCommand = 0xB6;
constexpr uint8_t SleepMode = 0x00;
constexpr uint8_t ForcedMode = 0x01;
constexpr uint8_t ModeMask = 0x03;
constexpr uint8_t NewDataMask = 0x80;
constexpr uint8_t GasRangeMask = 0x0F;
constexpr uint8_t GasValidMask = 0x20;
constexpr uint8_t HeaterStableMask = 0x10;
constexpr uint8_t RunGasEnable = 0x20;

constexpr size_t LenCoeff1 = 23;
constexpr size_t LenCoeff2 = 14;
constexpr size_t LenCoeff3 = 5;
constexpr size_t LenCoeffAll = LenCoeff1 + LenCoeff2 + LenCoeff3;
constexpr size_t LenField = 17;

constexpr size_t IdxDtk1CLsb = 0;
constexpr size_t IdxDtk1CMsb = 1;
constexpr size_t IdxDtk2C = 2;
constexpr size_t IdxSCLsb = 4;
constexpr size_t IdxSCMsb = 5;
constexpr size_t IdxTk1sCLsb = 6;
constexpr size_t IdxTk1sCMsb = 7;
constexpr size_t IdxTk2sC = 8;
constexpr size_t IdxTk3sC = 9;
constexpr size_t IdxOCLsb = 10;
constexpr size_t IdxOCMsb = 11;
constexpr size_t IdxTk10CLsb = 12;
constexpr size_t IdxTk10CMsb = 13;
constexpr size_t IdxTk20C = 14;
constexpr size_t IdxTk30C = 15;
constexpr size_t IdxNlsCLsb = 18;
constexpr size_t IdxNlsCMsb = 19;
constexpr size_t IdxTknlsC = 20;
constexpr size_t IdxNls3C = 21;
constexpr size_t IdxSHMsb = 23;
constexpr size_t IdxSHLsb = 24;
constexpr size_t IdxOHLsb = 24;
constexpr size_t IdxOHMsb = 25;
constexpr size_t IdxTk10hC = 26;
constexpr size_t IdxParH4 = 27;
constexpr size_t IdxParH3 = 28;
constexpr size_t IdxHlin2C = 29;
constexpr size_t IdxDoCLsb = 31;
constexpr size_t IdxDoCMsb = 32;
constexpr size_t IdxTkrCLsb = 33;
constexpr size_t IdxTkrCMsb = 34;
constexpr size_t IdxRoC = 35;
constexpr size_t IdxTAmbComp = 36;
constexpr size_t IdxResHeatVal = 37;
constexpr size_t IdxResHeatRange = 39;
constexpr size_t IdxRangeSwErr = 41;

uint16_t concatU16(uint8_t msb, uint8_t lsb)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(msb) << 8U) | lsb);
}

uint8_t oversamplingCode(config::Bme690Oversampling oversampling)
{
    return static_cast<uint8_t>(oversampling);
}

OperationResult i2cError()
{
    return {false, toErrorFlags(ErrorFlag::I2cFailure) |
                       toErrorFlags(ErrorFlag::CommunicationFailure)};
}

}  // namespace

BME690Driver::BME690Driver(hardware::II2CBus& bus, const BME690Config& config)
    : bus_(bus), config_(config)
{
}

OperationResult BME690Driver::begin()
{
    calibrationLoaded_ = false;
    if (!config_.addressConfigured || !config_.busAssignmentConfirmed || !bus_.isConfigured()) {
        status_ = {false, false, toErrorFlags(ErrorFlag::DeviceNotConfigured)};
        return {false, status_.errorFlags};
    }

    OperationResult result = writeRegister(RegSoftReset, SoftResetCommand);
    if (!result.ok) {
        status_ = {true, false, result.errorFlags};
        return result;
    }

    uint8_t chipId = 0;
    result = readRegister(RegChipId, chipId);
    if (!result.ok) {
        status_ = {true, false, result.errorFlags};
        return result;
    }
    if (chipId != Bme690ChipId) {
        status_ = {true, false, toErrorFlags(ErrorFlag::DeviceNotDetected)};
        return {false, status_.errorFlags};
    }

    uint8_t variantId = 0;
    result = readRegister(RegVariantId, variantId);
    if (!result.ok) {
        status_ = {true, false, result.errorFlags};
        return result;
    }

    result = loadCalibration();
    if (!result.ok) {
        status_ = {true, true, result.errorFlags | toErrorFlags(ErrorFlag::MissingCalibration)};
        return {false, status_.errorFlags};
    }

    result = configureForcedMode();
    if (!result.ok) {
        status_ = {true, true, result.errorFlags};
        return result;
    }

    status_ = {true, true, 0};
    return {true, 0};
}

DriverStatus BME690Driver::status() const
{
    return status_;
}

OperationResult BME690Driver::readMeasurement(Bme690Measurement& measurement)
{
    if (!status_.configured || !status_.detected || status_.errorFlags != 0) {
        measurement.valid = false;
        measurement.errorFlags = status_.errorFlags | toErrorFlags(ErrorFlag::DeviceNotConfigured);
        return {false, measurement.errorFlags};
    }
    if (!calibrationLoaded_) {
        measurement.valid = false;
        measurement.errorFlags = toErrorFlags(ErrorFlag::MissingCalibration);
        return {false, measurement.errorFlags};
    }

    OperationResult result = configureForcedMode();
    if (result.ok) {
        result = setForcedMode();
    }
    if (!result.ok) {
        measurement.valid = false;
        measurement.errorFlags = result.errorFlags;
        return result;
    }

    std::vector<uint8_t> field(LenField);
    ErrorFlags errors = 0;
    bool fresh = false;
    for (uint8_t attempt = 0; attempt < 5U; ++attempt) {
        result = readRegisters(RegField0, field);
        if (!result.ok) {
            measurement.valid = false;
            measurement.errorFlags = result.errorFlags;
            return result;
        }

        fresh = (field[0] & NewDataMask) != 0U;
        if (fresh) {
            break;
        }
        errors |= toErrorFlags(ErrorFlag::NotReady);
    }

    if (!fresh) {
        measurement.valid = false;
        measurement.measurementStatus = field[0];
        measurement.errorFlags = errors | toErrorFlags(ErrorFlag::Timeout);
        return {false, measurement.errorFlags};
    }

    const uint32_t adcPressure =
        (static_cast<uint32_t>(field[2]) << 16U) |
        (static_cast<uint32_t>(field[3]) << 8U) |
        static_cast<uint32_t>(field[4]);
    const uint32_t adcTemperature =
        (static_cast<uint32_t>(field[5]) << 16U) |
        (static_cast<uint32_t>(field[6]) << 8U) |
        static_cast<uint32_t>(field[7]);
    const uint16_t adcHumidity =
        static_cast<uint16_t>((static_cast<uint16_t>(field[8]) << 8U) | field[9]);
    const uint16_t adcGasResistance =
        static_cast<uint16_t>((static_cast<uint16_t>(field[15]) << 2U) |
                              (static_cast<uint16_t>(field[16]) >> 6U));
    const uint8_t gasRange = field[16] & GasRangeMask;

    measurement.temperatureC = compensateTemperatureC(adcTemperature);
    measurement.pressurePa = compensatePressurePa(adcPressure, measurement.temperatureC);
    measurement.humidityRh = compensateHumidityRh(adcHumidity, measurement.temperatureC);
    measurement.gasResistanceOhm = compensateGasResistanceOhm(adcGasResistance, gasRange);
    measurement.measurementStatus = static_cast<uint32_t>((field[0] & NewDataMask) |
                                                          (field[16] & GasValidMask) |
                                                          (field[16] & HeaterStableMask));
    measurement.gasValid = (field[16] & GasValidMask) != 0U;
    measurement.heaterStable = (field[16] & HeaterStableMask) != 0U;
    measurement.valid = measurement.gasValid && measurement.heaterStable &&
                        std::isfinite(measurement.temperatureC) &&
                        std::isfinite(measurement.humidityRh) &&
                        std::isfinite(measurement.pressurePa) &&
                        std::isfinite(measurement.gasResistanceOhm);

    errors = measurement.valid ? 0 : toErrorFlags(ErrorFlag::InvalidMeasurement);
    if (!measurement.gasValid) {
        errors |= toErrorFlags(ErrorFlag::InvalidMeasurement);
    }
    if (!measurement.heaterStable) {
        errors |= toErrorFlags(ErrorFlag::HeaterNotStable);
    }

    measurement.errorFlags = errors;
    measurement.monotonicTimestamp = std::chrono::steady_clock::now();
    measurement.wallTimestamp = std::chrono::system_clock::now();
    return {measurement.valid, measurement.errorFlags};
}

OperationResult BME690Driver::readRegister(uint8_t reg, uint8_t& value)
{
    std::vector<uint8_t> values(1);
    OperationResult result = readRegisters(reg, values);
    if (result.ok) {
        value = values[0];
    }
    return result;
}

OperationResult BME690Driver::readRegisters(uint8_t reg, std::vector<uint8_t>& values)
{
    const hardware::HardwareResult result = bus_.writeRead(config_.i2cAddress, {reg}, values);
    if (!result.ok) {
        return i2cError();
    }
    if (values.empty()) {
        return {false, toErrorFlags(ErrorFlag::InvalidMeasurement)};
    }
    return {true, 0};
}

OperationResult BME690Driver::writeRegister(uint8_t reg, uint8_t value)
{
    const hardware::HardwareResult result = bus_.write(config_.i2cAddress, {reg, value});
    if (!result.ok) {
        return i2cError();
    }
    return {true, 0};
}

OperationResult BME690Driver::loadCalibration()
{
    std::vector<uint8_t> coeff(LenCoeffAll, 0);
    std::vector<uint8_t> block1(LenCoeff1);
    std::vector<uint8_t> block2(LenCoeff2);
    std::vector<uint8_t> block3(LenCoeff3);

    OperationResult result = readRegisters(RegCoeff1, block1);
    if (!result.ok) {
        return result;
    }
    result = readRegisters(RegCoeff2, block2);
    if (!result.ok) {
        return result;
    }
    result = readRegisters(RegCoeff3, block3);
    if (!result.ok) {
        return result;
    }

    std::copy(block1.begin(), block1.end(), coeff.begin());
    std::copy(block2.begin(), block2.end(), coeff.begin() + LenCoeff1);
    std::copy(block3.begin(), block3.end(), coeff.begin() + LenCoeff1 + LenCoeff2);

    calibration_.parT1 = concatU16(coeff[IdxDoCMsb], coeff[IdxDoCLsb]);
    calibration_.parT2 = concatU16(coeff[IdxDtk1CMsb], coeff[IdxDtk1CLsb]);
    calibration_.parT3 = static_cast<int8_t>(coeff[IdxDtk2C]);
    calibration_.parP5 = static_cast<int16_t>(concatU16(coeff[IdxSCMsb], coeff[IdxSCLsb]));
    calibration_.parP6 = static_cast<int16_t>(concatU16(coeff[IdxTk1sCMsb], coeff[IdxTk1sCLsb]));
    calibration_.parP7 = static_cast<int8_t>(coeff[IdxTk2sC]);
    calibration_.parP8 = static_cast<int8_t>(coeff[IdxTk3sC]);
    calibration_.parP1 = static_cast<int16_t>(concatU16(coeff[IdxOCMsb], coeff[IdxOCLsb]));
    calibration_.parP2 = concatU16(coeff[IdxTk10CMsb], coeff[IdxTk10CLsb]);
    calibration_.parP3 = static_cast<int8_t>(coeff[IdxTk20C]);
    calibration_.parP4 = static_cast<int8_t>(coeff[IdxTk30C]);
    calibration_.parP9 = static_cast<int16_t>(concatU16(coeff[IdxNlsCMsb], coeff[IdxNlsCLsb]));
    calibration_.parP10 = static_cast<int8_t>(coeff[IdxTknlsC]);
    calibration_.parP11 = static_cast<int8_t>(coeff[IdxNls3C]);

    calibration_.parH5 = static_cast<int16_t>((static_cast<int16_t>(coeff[IdxSHMsb]) << 4U) |
                                              (coeff[IdxSHLsb] >> 4U));
    if (calibration_.parH5 > 2047) {
        calibration_.parH5 = static_cast<int16_t>(calibration_.parH5 - 4096);
    }
    calibration_.parH1 = static_cast<int16_t>((static_cast<int16_t>(coeff[IdxOHMsb]) << 4U) |
                                              (coeff[IdxOHLsb] & 0x0F));
    if (calibration_.parH1 > 2047) {
        calibration_.parH1 = static_cast<int16_t>(calibration_.parH1 - 4096);
    }
    calibration_.parH2 = static_cast<int8_t>(coeff[IdxTk10hC]);
    calibration_.parH4 = static_cast<int8_t>(coeff[IdxParH4]);
    calibration_.parH3 = coeff[IdxParH3];
    calibration_.parH6 = coeff[IdxHlin2C];

    calibration_.parG1 = static_cast<int8_t>(coeff[IdxRoC]);
    calibration_.parG2 = static_cast<int16_t>(concatU16(coeff[IdxTkrCMsb], coeff[IdxTkrCLsb]));
    calibration_.parG3 = static_cast<int8_t>(coeff[IdxTAmbComp]);
    calibration_.resHeatRange = static_cast<uint8_t>((coeff[IdxResHeatRange] & 0x30U) >> 4U);
    calibration_.resHeatVal = static_cast<int8_t>(coeff[IdxResHeatVal]);
    calibration_.rangeSwErr = static_cast<int8_t>((coeff[IdxRangeSwErr] & 0xF0U) / 16U);

    calibrationLoaded_ = true;
    return {true, 0};
}

OperationResult BME690Driver::configureForcedMode()
{
    OperationResult result = setSleepMode();
    if (!result.ok) {
        return result;
    }

    const uint8_t osHumidity = oversamplingCode(config_.runtime.humidityOversampling);
    const uint8_t osTemperature = oversamplingCode(config_.runtime.temperatureOversampling);
    const uint8_t osPressure = oversamplingCode(config_.runtime.pressureOversampling);
    const uint8_t filter = static_cast<uint8_t>(config_.runtime.iirFilterCoefficient & 0x07U);

    result = writeRegister(RegCtrlHum, static_cast<uint8_t>(osHumidity & 0x07U));
    if (!result.ok) {
        return result;
    }
    result = writeRegister(RegConfig, static_cast<uint8_t>(filter << 2U));
    if (!result.ok) {
        return result;
    }
    result = writeRegister(RegCtrlMeas, static_cast<uint8_t>((osTemperature << 5U) |
                                                            (osPressure << 2U) |
                                                            SleepMode));
    if (!result.ok) {
        return result;
    }
    result = writeRegister(RegResHeat0, heaterResistanceRegister(config_.runtime.heaterTemperatureC));
    if (!result.ok) {
        return result;
    }
    result = writeRegister(RegGasWait0,
                           heaterDurationRegister(static_cast<uint16_t>(config_.runtime.heaterDuration.count())));
    if (!result.ok) {
        return result;
    }
    result = writeRegister(RegCtrlGas0, 0x00);
    if (!result.ok) {
        return result;
    }
    return writeRegister(RegCtrlGas1, RunGasEnable);
}

OperationResult BME690Driver::setSleepMode()
{
    uint8_t ctrlMeas = 0;
    OperationResult result = readRegister(RegCtrlMeas, ctrlMeas);
    if (!result.ok) {
        return result;
    }
    if ((ctrlMeas & ModeMask) == SleepMode) {
        return {true, 0};
    }
    ctrlMeas = static_cast<uint8_t>(ctrlMeas & ~ModeMask);
    return writeRegister(RegCtrlMeas, ctrlMeas);
}

OperationResult BME690Driver::setForcedMode()
{
    uint8_t ctrlMeas = 0;
    OperationResult result = readRegister(RegCtrlMeas, ctrlMeas);
    if (!result.ok) {
        return result;
    }
    ctrlMeas = static_cast<uint8_t>((ctrlMeas & ~ModeMask) | ForcedMode);
    return writeRegister(RegCtrlMeas, ctrlMeas);
}

float BME690Driver::compensateTemperatureC(uint32_t adcTemperature) const
{
    const int32_t do1 = static_cast<int32_t>(calibration_.parT1) << 8;
    const double dtk1 = static_cast<double>(calibration_.parT2) / static_cast<double>(1ULL << 30U);
    const double dtk2 = static_cast<double>(calibration_.parT3) / static_cast<double>(1ULL << 48U);
    const double cf = static_cast<double>(adcTemperature) - static_cast<double>(do1);
    return static_cast<float>((cf * dtk1) + (cf * cf * dtk2));
}

float BME690Driver::compensatePressurePa(uint32_t adcPressure, float temperatureC) const
{
    const double temp = static_cast<double>(temperatureC);
    const double o = static_cast<double>(static_cast<uint32_t>(calibration_.parP1) * (1ULL << 3U));
    const double tk10 = static_cast<double>(calibration_.parP2) / static_cast<double>(1ULL << 6U);
    const double tk20 = static_cast<double>(calibration_.parP3) / static_cast<double>(1ULL << 8U);
    const double tk30 = static_cast<double>(calibration_.parP4) / static_cast<double>(1ULL << 15U);
    const double s = (static_cast<double>(calibration_.parP5) - static_cast<double>(1ULL << 14U)) /
                     static_cast<double>(1ULL << 20U);
    const double tk1s = (static_cast<double>(calibration_.parP6) - static_cast<double>(1ULL << 14U)) /
                        static_cast<double>(1ULL << 29U);
    const double tk2s = static_cast<double>(calibration_.parP7) / static_cast<double>(1ULL << 32U);
    const double tk3s = static_cast<double>(calibration_.parP8) / static_cast<double>(1ULL << 37U);
    const double nls = static_cast<double>(calibration_.parP9) / static_cast<double>(1ULL << 48U);
    const double tknls = static_cast<double>(calibration_.parP10) / static_cast<double>(1ULL << 48U);
    const double nls3 = static_cast<double>(calibration_.parP11) /
                        (static_cast<double>(1ULL << 35U) * static_cast<double>(1ULL << 30U));
    const double adc = static_cast<double>(adcPressure);
    const double offset = o + (tk10 * temp) + (tk20 * temp * temp) + (tk30 * temp * temp * temp);
    const double sensitivity = adc * (s + (tk1s * temp) + (tk2s * temp * temp) +
                                      (tk3s * temp * temp * temp));
    return static_cast<float>(offset + sensitivity +
                              (adc * adc * (nls + (tknls * temp))) +
                              (adc * adc * adc * nls3));
}

float BME690Driver::compensateHumidityRh(uint16_t adcHumidity, float temperatureC) const
{
    const double tempComp = (static_cast<double>(temperatureC) * 5120.0) - 76800.0;
    const double oh = static_cast<double>(calibration_.parH1) * static_cast<double>(1ULL << 6U);
    const double sh = static_cast<double>(calibration_.parH5) / static_cast<double>(1ULL << 16U);
    const double tk10h = static_cast<double>(calibration_.parH2) / static_cast<double>(1ULL << 14U);
    const double tk1sh = static_cast<double>(calibration_.parH4) / static_cast<double>(1ULL << 26U);
    const double tk2sh = static_cast<double>(calibration_.parH3) / static_cast<double>(1ULL << 26U);
    const double hlin2 = static_cast<double>(calibration_.parH6) / static_cast<double>(1ULL << 19U);
    const double hoff = static_cast<double>(adcHumidity) - (oh + (tk10h * tempComp));
    const double hsens = hoff * sh * (1.0 + (tk1sh * tempComp) +
                                      (tk1sh * tk2sh * tempComp * tempComp));
    const double humidity = hsens * (1.0 - (hlin2 * hsens));
    return static_cast<float>(std::max(0.0, std::min(100.0, humidity)));
}

float BME690Driver::compensateGasResistanceOhm(uint16_t adcGasResistance, uint8_t gasRange)
{
    if (gasRange > 15U) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const uint32_t var1 = UINT32_C(262144) >> gasRange;
    int32_t var2 = static_cast<int32_t>(adcGasResistance) - INT32_C(512);
    var2 = INT32_C(4096) + (var2 * INT32_C(3));
    if (var2 == 0) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return 1000000.0F * static_cast<float>(var1) / static_cast<float>(var2);
}

uint8_t BME690Driver::heaterResistanceRegister(uint16_t heaterTemperatureC) const
{
    const float temp = static_cast<float>(std::min<uint16_t>(heaterTemperatureC, 400));
    const float var1 = (static_cast<float>(calibration_.parG1) / 16.0F) + 49.0F;
    const float var2 = ((static_cast<float>(calibration_.parG2) / 32768.0F) * 0.0005F) + 0.00235F;
    const float var3 = static_cast<float>(calibration_.parG3) / 1024.0F;
    const float var4 = var1 * (1.0F + (var2 * temp));
    const float var5 = var4 + (var3 * static_cast<float>(ambientTemperatureC_));
    const float rangeFactor = 4.0F / (4.0F + static_cast<float>(calibration_.resHeatRange));
    const float heatValFactor = 1.0F / (1.0F + (static_cast<float>(calibration_.resHeatVal) * 0.002F));
    const float registerValue = 3.4F * ((var5 * rangeFactor * heatValFactor) - 25.0F);
    return static_cast<uint8_t>(std::max(0.0F, std::min(255.0F, registerValue)));
}

uint8_t BME690Driver::heaterDurationRegister(uint16_t durationMs)
{
    uint8_t factor = 0;
    if (durationMs >= 0x0FC0U) {
        return 0xFF;
    }
    while (durationMs > 0x3FU) {
        durationMs = static_cast<uint16_t>(durationMs / 4U);
        ++factor;
    }
    return static_cast<uint8_t>(durationMs + (factor * 64U));
}

}  // namespace odor
