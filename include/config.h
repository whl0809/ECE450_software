#pragma once

#include <array>
#include <chrono>
#include <cstdint>

namespace odor::config {

constexpr const char* FirmwareScaffoldVersion = "scaffold-0.1";
constexpr const char* TargetName = "Raspberry Pi 5 / Raspberry Pi OS";

constexpr bool EnableDebugLogging = true;
constexpr bool EnableCsvOutput = false;

constexpr bool EnableTgsArray = true;
constexpr bool EnableNh3Sensor = true;
constexpr bool EnableH2sSensor = true;
constexpr bool EnableSgp41 = true;
constexpr bool EnableBme690 = true;
constexpr bool EnableSht45 = true;

constexpr uint8_t Sgp41I2cAddress = 0x59;
constexpr uint8_t Sht45I2cAddress = 0x44;
constexpr uint8_t Nh3Mcp3421I2cAddress = 0x69;
constexpr uint8_t H2sMcp3421I2cAddress = 0x6A;
constexpr uint8_t Bme690I2cAddress = 0x76;
constexpr uint32_t RawSchemaVersion = 1;

constexpr size_t TgsChannelCount = 6;

enum class TgsModel {
    Tgs2610,
    Tgs2620,
    Tgs2603,
    Tgs2602,
    Tgs2600,
    Tgs2611,
};

struct TgsChannelConfig {
    TgsModel model;
    uint8_t ads114s06Ain;
    const char* signalName;
};

constexpr std::array<TgsChannelConfig, TgsChannelCount> TgsChannels = {{
    {TgsModel::Tgs2610, 0, "TGS2610_VOUT"},
    {TgsModel::Tgs2620, 1, "TGS2620_VOUT"},
    {TgsModel::Tgs2603, 2, "TGS2603_VOUT"},
    {TgsModel::Tgs2602, 3, "TGS2602_VOUT"},
    {TgsModel::Tgs2600, 4, "TGS2600_VOUT"},
    {TgsModel::Tgs2611, 5, "TGS2611_VOUT"},
}};

constexpr float TgsApproximateLoadResistanceOhm = 10000.0F;

constexpr float Ads114s06ReferenceVoltageV = 4.096F;
constexpr bool Ads114s06ChipSelectPermanentlyAsserted = true;
constexpr bool Ads114s06ResetControlledByRaspberryPi = false;

enum class ElectrochemicalElectrodeMode {
    ThreeElectrodeProvisional,
};

enum class JumperState {
    Open,
    Closed,
};

struct ElectrochemicalFrontEndConfig {
    ElectrochemicalElectrodeMode electrodeMode;
    JumperState jp2State;
    float vbiasNominalV;
    float tiaFeedbackResistanceOhm;
    float tiaFeedbackCapacitanceF;
    float tiaInputSeriesResistanceOhm;
    float outputLowPassResistanceOhm;
    float outputLowPassCapacitanceF;
    bool signalPolarityValidated;
};

constexpr ElectrochemicalFrontEndConfig Nh3FrontEnd = {
    ElectrochemicalElectrodeMode::ThreeElectrodeProvisional,
    JumperState::Open,
    1.65F,
    1000000.0F,
    0.000010F,
    100.0F,
    49900.0F,
    0.000010F,
    false,
};

constexpr ElectrochemicalFrontEndConfig H2sFrontEnd = {
    ElectrochemicalElectrodeMode::ThreeElectrodeProvisional,
    JumperState::Open,
    1.65F,
    1000000.0F,
    0.000010F,
    100.0F,
    49900.0F,
    0.000010F,
    false,
};

// TODO: Tune each interval after device timing requirements and target sample
// rates are confirmed. These are scheduler placeholders only.
constexpr std::chrono::milliseconds SensorManagerUpdateInterval{100};
constexpr std::chrono::milliseconds HeartbeatInterval{1000};
constexpr std::chrono::milliseconds TgsSampleInterval{1000};
constexpr std::chrono::milliseconds ElectrochemicalSampleInterval{1000};
constexpr std::chrono::milliseconds Sgp41SampleInterval{1000};
constexpr std::chrono::milliseconds Bme690SampleInterval{1000};
constexpr std::chrono::milliseconds Sht45SampleInterval{1000};

enum class Sht45Precision {
    High,
    Medium,
    Low,
};

struct Sht45RuntimeSettings {
    std::chrono::milliseconds sampleInterval{1000};
    Sht45Precision precision = Sht45Precision::High;
    bool heaterEnabled = false;
    bool crcEnabled = true;
    std::chrono::milliseconds conversionTimeout{20};
};

struct Sgp41RuntimeSettings {
    std::chrono::milliseconds sampleInterval{1000};
    std::chrono::milliseconds compensationMaxAge{5000};
    bool useSht45Compensation = true;
    bool retainRawSignals = true;
    std::chrono::milliseconds measurementTimeout{80};
};

enum class Bme690Oversampling {
    Skipped = 0,
    X1 = 1,
    X2 = 2,
    X4 = 3,
    X8 = 4,
    X16 = 5,
};

struct Bme690RuntimeSettings {
    uint8_t i2cAddress = Bme690I2cAddress;
    Bme690Oversampling temperatureOversampling = Bme690Oversampling::X2;
    Bme690Oversampling humidityOversampling = Bme690Oversampling::X1;
    Bme690Oversampling pressureOversampling = Bme690Oversampling::X4;
    uint8_t iirFilterCoefficient = 3;
    uint16_t heaterTemperatureC = 320;
    std::chrono::milliseconds heaterDuration{150};
    std::chrono::milliseconds measurementInterval{1000};
    std::chrono::milliseconds measurementTimeout{250};
};

enum class Mcp3421Resolution {
    Bits12,
    Bits14,
    Bits16,
    Bits18,
};

enum class Mcp3421Gain {
    X1,
    X2,
    X4,
    X8,
};

enum class Mcp3421ConversionMode {
    OneShot,
    Continuous,
};

struct Mcp3421RuntimeSettings {
    Mcp3421Resolution resolution = Mcp3421Resolution::Bits16;
    Mcp3421Gain gain = Mcp3421Gain::X1;
    Mcp3421ConversionMode conversionMode = Mcp3421ConversionMode::OneShot;
    float fullScaleVoltageV = 2.048F;
    std::chrono::milliseconds conversionTimeout{100};
};

enum class Ads114s06PgaGain {
    X1 = 1,
    X2 = 2,
    X4 = 4,
    X8 = 8,
    X16 = 16,
    X32 = 32,
    X64 = 64,
    X128 = 128,
};

struct Ads114s06RuntimeSettings {
    uint8_t spiMode = 1;
    uint8_t bitsPerWord = 8;
    uint32_t maxSpeedHz = 1000000;
    Ads114s06PgaGain pgaGain = Ads114s06PgaGain::X1;
    uint8_t dataRateCode = 0x14;
    uint8_t filterCode = 0x00;
    bool verifyRegisterReadback = true;
    bool waitForDrdyWhenConfigured = true;
    std::chrono::milliseconds conversionTimeout{250};
};

struct RawLoggingSettings {
    bool enabled = false;
    uint32_t schemaVersion = RawSchemaVersion;
    const char* outputPath = "";
};

constexpr Sht45RuntimeSettings Sht45Defaults{};
constexpr Sgp41RuntimeSettings Sgp41Defaults{};
constexpr Bme690RuntimeSettings Bme690Defaults{};
constexpr Mcp3421RuntimeSettings Mcp3421Defaults{};
constexpr Ads114s06RuntimeSettings Ads114s06Defaults{};
constexpr RawLoggingSettings RawLoggingDefaults{};

}  // namespace odor::config
