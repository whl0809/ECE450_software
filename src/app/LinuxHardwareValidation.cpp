#include "app/LinuxHardwareValidation.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <gpiod.h>
#include <string>
#include <unistd.h>

namespace odor::app {

namespace {

void addErrnoError(ValidationResult& result, const std::string& context)
{
    result.ok = false;
    result.errors.push_back(context + ": " + std::strerror(errno));
}

void validateOpenReadWrite(ValidationResult& result, const std::string& path, const std::string& label)
{
    const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        addErrnoError(result, "open " + label + " " + path);
        return;
    }
    (void)::close(fd);
}

void validateGpioLineRequest(ValidationResult& result,
                             gpiod_chip* chip,
                             unsigned int lineOffset,
                             const char* consumer,
                             bool output,
                             bool activeLow)
{
    gpiod_line* line = gpiod_chip_get_line(chip, lineOffset);
    if (line == nullptr) {
        addErrnoError(result, "get GPIO line " + std::to_string(lineOffset));
        return;
    }

    const int flags = activeLow ? GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW : 0;
    int requested = 0;
    if (output) {
        requested = gpiod_line_request_output_flags(line, consumer, flags, 0);
    } else {
        requested = gpiod_line_request_input_flags(line, consumer, flags);
    }
    if (requested < 0) {
        addErrnoError(result, "request GPIO line " + std::to_string(lineOffset));
        return;
    }
    gpiod_line_release(line);
}

}  // namespace

ValidationResult validateLinuxHardwareAccess(const RuntimeConfig& config)
{
    ValidationResult result;
    validateOpenReadWrite(result, config.primaryI2c.path, "I2C adapter");

    if (!config.secondaryI2c.path.empty() && config.secondaryI2c.path != config.primaryI2c.path) {
        validateOpenReadWrite(result, config.secondaryI2c.path, "secondary I2C adapter");
    }

    validateOpenReadWrite(result, config.adsSpiDevice, "SPI device");

    gpiod_chip* chip = gpiod_chip_open(config.gpioChipPath.c_str());
    if (chip == nullptr) {
        addErrnoError(result, "open GPIO chip " + config.gpioChipPath);
        return result;
    }

    const char* label = gpiod_chip_label(chip);
    const std::string actualLabel = label == nullptr ? "" : label;
    if (actualLabel != config.gpioChipExpectedLabel) {
        result.ok = false;
        result.errors.push_back("GPIO chip label mismatch for " + config.gpioChipPath +
                                ": expected " + config.gpioChipExpectedLabel +
                                ", got " + actualLabel);
    }

    validateGpioLineRequest(result,
                            chip,
                            config.adsStart.lineOffset,
                            "odor-sensing-validate-start",
                            true,
                            config.adsStart.activeLow);
    validateGpioLineRequest(result,
                            chip,
                            config.adsDrdy.lineOffset,
                            "odor-sensing-validate-drdy",
                            false,
                            config.adsDrdy.activeLow);

    gpiod_chip_close(chip);
    return result;
}

}  // namespace odor::app
