#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace odor::protocol {

uint8_t sensirionCrc8(const uint8_t* data, size_t length);
bool sensirionCrc8Matches(const uint8_t* data, size_t length, uint8_t expected);
void appendU16WithSensirionCrc(std::vector<uint8_t>& bytes, uint16_t value);

int32_t signExtend(uint32_t value, uint8_t bitCount);
uint16_t readU16Be(const std::vector<uint8_t>& bytes, size_t offset);
int32_t readSignedBe(const std::vector<uint8_t>& bytes, size_t offset, size_t byteCount);

uint16_t shtCompensationHumidityTicks(float humidityRh);
uint16_t shtCompensationTemperatureTicks(float temperatureC);

}  // namespace odor::protocol
