#include "hardware/linux/LinuxI2CBus.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
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

LinuxI2CBus::LinuxI2CBus(std::string adapterPath)
    : adapterPath_(std::move(adapterPath))
{
}

LinuxI2CBus::~LinuxI2CBus()
{
    close();
}

HardwareResult LinuxI2CBus::open()
{
    if (adapterPath_.empty()) {
        return HardwareResult::failure(-EINVAL, "I2C adapter path is not configured");
    }

    fd_ = ::open(adapterPath_.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        return errnoFailure("open " + adapterPath_);
    }

    return HardwareResult::success();
}

void LinuxI2CBus::close()
{
    if (fd_ >= 0) {
        (void)::close(fd_);
        fd_ = -1;
    }
}

bool LinuxI2CBus::isConfigured() const
{
    return fd_ >= 0;
}

std::string LinuxI2CBus::description() const
{
    return adapterPath_.empty() ? "unconfigured-linux-i2c" : adapterPath_;
}

HardwareResult LinuxI2CBus::selectAddress(uint8_t address)
{
    if (fd_ < 0) {
        return HardwareResult::failure(-ENODEV, "I2C adapter is not open");
    }
    if (::ioctl(fd_, I2C_SLAVE, address) < 0) {
        return errnoFailure("select I2C address");
    }
    return HardwareResult::success();
}

HardwareResult LinuxI2CBus::write(uint8_t address, const std::vector<uint8_t>& bytes)
{
    HardwareResult selected = selectAddress(address);
    if (!selected.ok) {
        return selected;
    }
    const ssize_t written = ::write(fd_, bytes.data(), bytes.size());
    if (written < 0 || static_cast<size_t>(written) != bytes.size()) {
        return errnoFailure("I2C write");
    }
    return HardwareResult::success();
}

HardwareResult LinuxI2CBus::read(uint8_t address, std::vector<uint8_t>& bytes)
{
    HardwareResult selected = selectAddress(address);
    if (!selected.ok) {
        return selected;
    }
    const ssize_t readCount = ::read(fd_, bytes.data(), bytes.size());
    if (readCount < 0 || static_cast<size_t>(readCount) != bytes.size()) {
        return errnoFailure("I2C read");
    }
    return HardwareResult::success();
}

HardwareResult LinuxI2CBus::writeRead(uint8_t address,
                                      const std::vector<uint8_t>& writeBytes,
                                      std::vector<uint8_t>& readBytes)
{
    HardwareResult written = write(address, writeBytes);
    if (!written.ok) {
        return written;
    }
    return read(address, readBytes);
}

}  // namespace odor::hardware::linux
