#include "protocol/ProtocolUtils.h"

#include <algorithm>
#include <cmath>

namespace odor::protocol {

uint8_t sensirionCrc8(const uint8_t* data, size_t length)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80U) != 0U ? static_cast<uint8_t>((crc << 1U) ^ 0x31U)
                                      : static_cast<uint8_t>(crc << 1U);
        }
    }
    return crc;
}

bool sensirionCrc8Matches(const uint8_t* data, size_t length, uint8_t expected)
{
    return sensirionCrc8(data, length) == expected;
}

void appendU16WithSensirionCrc(std::vector<uint8_t>& bytes, uint16_t value)
{
    const uint8_t pair[2] = {
        static_cast<uint8_t>((value >> 8U) & 0xFFU),
        static_cast<uint8_t>(value & 0xFFU),
    };
    bytes.push_back(pair[0]);
    bytes.push_back(pair[1]);
    bytes.push_back(sensirionCrc8(pair, 2));
}

int32_t signExtend(uint32_t value, uint8_t bitCount)
{
    const uint32_t signBit = 1UL << (bitCount - 1U);
    const uint32_t mask = (bitCount >= 32U) ? 0xFFFFFFFFUL : ((1UL << bitCount) - 1UL);
    value &= mask;
    return static_cast<int32_t>((value ^ signBit) - signBit);
}

uint16_t readU16Be(const std::vector<uint8_t>& bytes, size_t offset)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes.at(offset)) << 8U) |
                                 static_cast<uint16_t>(bytes.at(offset + 1U)));
}

int32_t readSignedBe(const std::vector<uint8_t>& bytes, size_t offset, size_t byteCount)
{
    uint32_t value = 0;
    for (size_t i = 0; i < byteCount; ++i) {
        value = (value << 8U) | bytes.at(offset + i);
    }
    return signExtend(value, static_cast<uint8_t>(byteCount * 8U));
}

uint16_t shtCompensationHumidityTicks(float humidityRh)
{
    const float clipped = std::max(0.0F, std::min(100.0F, humidityRh));
    return static_cast<uint16_t>(std::lround((clipped * 65535.0F) / 100.0F));
}

uint16_t shtCompensationTemperatureTicks(float temperatureC)
{
    const float clipped = std::max(-45.0F, std::min(130.0F, temperatureC));
    return static_cast<uint16_t>(std::lround(((clipped + 45.0F) * 65535.0F) / 175.0F));
}

}  // namespace odor::protocol
