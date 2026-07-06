#include "hardware/linux/LinuxSPIDevice.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>

namespace odor::hardware::linux {

namespace {

HardwareResult errnoFailure(const std::string& context)
{
    return HardwareResult::failure(-errno, context + ": " + std::strerror(errno));
}

}  // namespace

LinuxSPIDevice::LinuxSPIDevice(LinuxSPIConfig config)
    : config_(std::move(config))
{
}

LinuxSPIDevice::~LinuxSPIDevice()
{
    close();
}

HardwareResult LinuxSPIDevice::open()
{
    if (config_.devicePath.empty() || config_.maxSpeedHz == 0U) {
        return HardwareResult::failure(-EINVAL, "SPI device path or speed is not configured");
    }

    fd_ = ::open(config_.devicePath.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        return errnoFailure("open " + config_.devicePath);
    }

    if (::ioctl(fd_, SPI_IOC_WR_MODE, &config_.mode) < 0 ||
        ::ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &config_.bitsPerWord) < 0 ||
        ::ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &config_.maxSpeedHz) < 0) {
        HardwareResult failed = errnoFailure("configure SPI device");
        close();
        return failed;
    }

    return HardwareResult::success();
}

void LinuxSPIDevice::close()
{
    if (fd_ >= 0) {
        (void)::close(fd_);
        fd_ = -1;
    }
}

bool LinuxSPIDevice::isConfigured() const
{
    return fd_ >= 0;
}

std::string LinuxSPIDevice::description() const
{
    return config_.devicePath.empty() ? "unconfigured-linux-spi" : config_.devicePath;
}

HardwareResult LinuxSPIDevice::transfer(const std::vector<uint8_t>& txBytes,
                                        std::vector<uint8_t>& rxBytes)
{
    if (fd_ < 0) {
        return HardwareResult::failure(-ENODEV, "SPI device is not open");
    }

    rxBytes.resize(txBytes.size());
    spi_ioc_transfer transfer{};
    transfer.tx_buf = reinterpret_cast<unsigned long>(txBytes.data());
    transfer.rx_buf = reinterpret_cast<unsigned long>(rxBytes.data());
    transfer.len = static_cast<uint32_t>(txBytes.size());
    transfer.speed_hz = config_.maxSpeedHz;
    transfer.bits_per_word = config_.bitsPerWord;

    if (::ioctl(fd_, SPI_IOC_MESSAGE(1), &transfer) < 0) {
        return errnoFailure("SPI transfer");
    }

    return HardwareResult::success();
}

}  // namespace odor::hardware::linux
