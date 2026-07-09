#include "app/RuntimeConfig.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <exception>
#include <sstream>
#include <stdexcept>
#include <string>

namespace odor::app {

namespace {

std::string trim(std::string text)
{
    const auto begin = std::find_if_not(text.begin(), text.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto end = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

std::string unquote(std::string value)
{
    value = trim(std::move(value));
    if (value.size() >= 2U && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2U);
    }
    return value;
}

std::string removeComment(const std::string& line)
{
    bool inString = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') {
            inString = !inString;
        } else if (line[i] == '#' && !inString) {
            return line.substr(0, i);
        }
    }
    return line;
}

bool parseBool(const std::string& value)
{
    const std::string normalized = trim(value);
    return normalized == "true" || normalized == "1";
}

uint32_t parseUint(const std::string& value)
{
    const std::string normalized = unquote(value);
    size_t parsed = 0;
    const unsigned long result = std::stoul(normalized, &parsed, 0);
    if (parsed != normalized.size()) {
        throw std::invalid_argument("invalid integer: " + normalized);
    }
    return static_cast<uint32_t>(result);
}

config::Ads114s06PgaGain parseAdsGain(const std::string& value)
{
    switch (parseUint(value)) {
    case 1:
        return config::Ads114s06PgaGain::X1;
    case 2:
        return config::Ads114s06PgaGain::X2;
    case 4:
        return config::Ads114s06PgaGain::X4;
    case 8:
        return config::Ads114s06PgaGain::X8;
    case 16:
        return config::Ads114s06PgaGain::X16;
    case 32:
        return config::Ads114s06PgaGain::X32;
    case 64:
        return config::Ads114s06PgaGain::X64;
    case 128:
        return config::Ads114s06PgaGain::X128;
    default:
        throw std::invalid_argument("unsupported ADS114S06 PGA gain");
    }
}

void setKnownValue(RuntimeConfig& config,
                   const std::string& section,
                   const std::string& key,
                   const std::string& value)
{
    if (section == "i2c.primary" && key == "path") {
        config.primaryI2c.path = unquote(value);
    } else if (section == "i2c.secondary" && key == "path") {
        config.secondaryI2c.path = unquote(value);
    } else if (section == "ads114s06.spi" && key == "device") {
        config.adsSpiDevice = unquote(value);
    } else if (section == "ads114s06.spi" && key == "mode") {
        config.adsSpiMode = static_cast<uint8_t>(parseUint(value));
    } else if (section == "ads114s06.spi" && key == "bits_per_word") {
        config.adsBitsPerWord = static_cast<uint8_t>(parseUint(value));
    } else if (section == "ads114s06.spi" && key == "max_speed_hz") {
        config.adsMaxSpeedHz = parseUint(value);
    } else if (section == "ads114s06.spi" && key == "pga_gain") {
        config.adsPgaGain = parseAdsGain(value);
    } else if (section == "ads114s06.spi" && key == "data_rate_code") {
        if (unquote(value).find("PROVISIONAL") != std::string::npos) {
            return;
        }
        config.adsDataRateCode = static_cast<uint8_t>(parseUint(value));
    } else if (section == "ads114s06.spi" && key == "filter_code") {
        if (unquote(value).find("PROVISIONAL") != std::string::npos) {
            return;
        }
        config.adsFilterCode = static_cast<uint8_t>(parseUint(value));
    } else if (section == "ads114s06.spi" && key == "verify_register_readback") {
        config.adsVerifyRegisterReadback = parseBool(value);
    } else if (section == "ads114s06.spi" && key == "wait_for_drdy_when_configured") {
        config.adsWaitForDrdy = parseBool(value);
    } else if (section == "ads114s06.gpio" && key == "gpiochip") {
        config.gpioChipPath = unquote(value);
    } else if (section == "ads114s06.gpio" && key == "expected_label") {
        config.gpioChipExpectedLabel = unquote(value);
    } else if (section == "ads114s06.gpio" && key == "start_line_offset") {
        config.adsStart.lineOffset = parseUint(value);
        config.adsStart.configured = true;
    } else if (section == "ads114s06.gpio" && key == "start_active_low") {
        config.adsStart.activeLow = parseBool(value);
    } else if (section == "ads114s06.gpio" && key == "drdy_line_offset") {
        config.adsDrdy.lineOffset = parseUint(value);
        config.adsDrdy.configured = true;
    } else if (section == "ads114s06.gpio" && key == "drdy_active_low") {
        config.adsDrdy.activeLow = parseBool(value);
    }
}

}  // namespace

ConfigLoadResult loadRuntimeConfig(const std::string& path)
{
    ConfigLoadResult result;
    std::ifstream input(path);
    if (!input) {
        result.errors.push_back("unable to open runtime configuration: " + path);
        return result;
    }

    std::string section;
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        line = trim(removeComment(line));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2U));
            continue;
        }

        const size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1U));
        try {
            setKnownValue(result.config, section, key, value);
        } catch (const std::exception& ex) {
            result.errors.push_back("config line " + std::to_string(lineNumber) + ": " + ex.what());
        }
    }

    result.ok = result.errors.empty();
    return result;
}

ValidationResult validateRuntimeConfigValues(const RuntimeConfig& config)
{
    ValidationResult result;
    auto require = [&](bool condition, const std::string& message) {
        if (!condition) {
            result.ok = false;
            result.errors.push_back(message);
        }
    };

    require(!config.primaryI2c.path.empty(), "primary I2C adapter path is missing");
    require(!config.adsSpiDevice.empty(), "ADS114S06 SPI device path is missing");
    require(config.adsSpiMode == 1U, "ADS114S06 SPI mode must be 1");
    require(config.adsBitsPerWord == 8U, "ADS114S06 SPI bits_per_word must be 8");
    require(config.adsMaxSpeedHz > 0U, "ADS114S06 SPI max_speed_hz must be nonzero");
    require(config.adsMaxSpeedHz <= 1000000U, "ADS114S06 provisional SPI clock must not exceed 1 MHz");
    require(!config.gpioChipPath.empty(), "GPIO chip path is missing");
    require(!config.gpioChipExpectedLabel.empty(), "expected GPIO chip label is missing");
    require(config.adsStart.configured, "ADS START GPIO line offset is missing");
    require(config.adsDrdy.configured, "ADS DRDY GPIO line offset is missing");
    if (config.adsStart.configured && config.adsDrdy.configured) {
        require(config.adsStart.lineOffset != config.adsDrdy.lineOffset,
                "ADS START and DRDY GPIO line offsets must differ");
    }
    if (config.adsStart.configured) {
        require(!config.adsStart.activeLow, "ADS START must be active high for this machine profile");
    }
    if (config.adsDrdy.configured) {
        require(config.adsDrdy.activeLow, "ADS DRDY# must be active low for this machine profile");
    }

    if (config.secondaryI2c.path.empty()) {
        result.warnings.push_back("secondary I2C adapter path is empty; primary adapter will be shared");
    }

    return result;
}

}  // namespace odor::app
