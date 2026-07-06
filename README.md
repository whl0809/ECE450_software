# ECE450 Odor-Sensing Raspberry Pi 5 Application

Linux user-space scaffold for the Raspberry Pi 5 version of the odor-sensing
project. The software platform is Raspberry Pi OS, CMake, and C++17.

This scaffold does not access real hardware by default. The executable starts,
initializes `SensorManager` with unconfigured mock hardware interfaces, handles
`SIGINT`/`SIGTERM`, and prints a non-blocking heartbeat.

## Hardware Architecture

```text
TGS2610 / TGS2620 / TGS2603 / TGS2602 / TGS2600 / TGS2611
    -> analog sensing circuits
    -> ADS114S06
    -> Linux spidev
    -> Raspberry Pi 5

ES1-NH3-100
    -> dedicated NH3 analog front end
    -> MCP3421A1T-E/CH at 0x69
    -> Linux I2C
    -> Raspberry Pi 5

ES1-H2S-100
    -> dedicated H2S analog front end
    -> MCP3421A2T-E/CH at 0x6A
    -> Linux I2C
    -> Raspberry Pi 5

SGP41-D-R4 at 0x59
    -> Linux I2C
    -> Raspberry Pi 5

SHT45-AD1F-R2 at 0x44
    -> Linux I2C
    -> Raspberry Pi 5

BME690 at 0x76
    -> Linux I2C
    -> Raspberry Pi 5
```

## Confirmed Board Contract

Board 1 `CN1` exposes `SENSOR_SCL`, `SENSOR_SDA`, and `DGND`; pins 5 and 6 are
not yet confirmed and must not be used.

Board 1 I2C devices:

```text
SGP41          0x59
NH3 MCP3421    0x69
H2S MCP3421    0x6A
BME690         0x76
```

Board 2 ADS114S06 connector:

```text
CN1 pin 1 -> START
CN1 pin 2 -> DIN   (Raspberry Pi MOSI)
CN1 pin 3 -> SCLK
CN1 pin 4 -> DOUT  (Raspberry Pi MISO)
CN1 pin 5 -> DRDY#
CN1 pin 6 -> AGND
```

ADS114S06 board wiring:

```text
CS#     -> DGND, permanently selected
RESET#  -> IOVDD, not Raspberry Pi controlled
CLK     -> DGND, internal clock configuration
REFP0   -> external 4.096 V REF5040AIDR reference
REFN0   -> AGND
```

Do not add an ADS114S06 chip-select GPIO. The ADC must be the only active
device on its physical SPI data lines.

TGS channel order:

```text
AIN0 -> TGS2610_VOUT
AIN1 -> TGS2620_VOUT
AIN2 -> TGS2603_VOUT
AIN3 -> TGS2602_VOUT
AIN4 -> TGS2600_VOUT
AIN5 -> TGS2611_VOUT
AINCOM -> AGND
```

The SHT45 pads on Board 2 are:

```text
Pad 1 -> SHT45_SDA
Pad 2 -> SHT45_SCL
Pad 3 -> VDD3V3
Pad 4 -> AGND
```

Under the current independent-power plan, connect Raspberry Pi SDA, SCL, and
GND only. Do not connect Raspberry Pi 3.3 V to SHT45 Pad 3 unless the power
architecture is intentionally changed and revalidated.

## Electrochemical Front Ends

Current provisional assembly assumption:

```text
NH3 JP2 = OPEN -> three-electrode sensor
H2S JP2 = OPEN -> three-electrode sensor
```

Both signal chains use OPA2192 analog front ends and MCP3421 differential
measurement with `VIN+` at TIA output and `VIN-` at `VBIAS`. Nominal metadata:

```text
VBIAS ~= 1.65 V from 10 kOhm / 10 kOhm divider
TIA feedback resistor = 1 MOhm
TIA feedback capacitor = 10 uF
TIA input series resistor = 100 Ohm
Output low-pass resistor = 49.9 kOhm
Output low-pass capacitor = 10 uF
```

Signal polarity, zero offset, sensitivity, and concentration calibration remain
unvalidated. Concentration values must not be reported as valid yet.

## Power Rules

```text
Raspberry Pi 5 -> independently powered
Board 1         -> independently powered through its own USB-C input
Board 2         -> independently powered through its own USB-C input
All three       -> common ground
```

Do not parallel the Raspberry Pi 3.3 V or 5 V rails with either board's
regulated supply. Evaluate combined I2C pull-up resistance and back-powering
before joining independently powered I2C domains.

## Software Structure

```text
include/
  config.h
  error_flags.h
  sensor_types.h
  drivers/
  hardware/
  services/

src/
  main.cpp
  drivers/
  hardware/linux/
  hardware/mock/
  services/

tests/unit/
config/odor-sensing.example.toml
```

## Raspberry Pi OS Dependencies

```bash
sudo apt install build-essential cmake libgpiod-dev
```

Do not edit boot configuration, device-tree overlays, udev rules, user groups,
or systemd services until the wiring and device-access plan are reviewed.

## Build And Test

Default Raspberry Pi build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On non-Linux development hosts, build the portable/mock scaffold without Linux
hardware wrappers:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DODOR_BUILD_LINUX_HARDWARE=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Run

```bash
./build/odor_sensing_app
```

Expected output:

```text
Odor Sensing Raspberry Pi 5 Application
Version: scaffold-0.1
Build: ...
Target: Raspberry Pi 5 / Raspberry Pi OS
Hardware access: disabled until runtime device paths are configured
SensorManager initialization: NOT CONFIGURED
Sensor interfaces are not yet enabled
heartbeat,uptime_ms=1000
```

Stop with `Ctrl+C`.

## Provisional Runtime Profile

The example TOML currently selects these provisional software defaults:

- SHT45 at 1 Hz, high precision, heater off, CRC required.
- SGP41 at 1 Hz using recent valid SHT45 compensation when available.
- BME690 at `0x76` with Bosch SensorAPI-derived forced-measurement compensation and heater setup.
- MCP3421 one-shot, 16-bit, gain x1, retaining signed raw code and differential voltage only.
- ADS114S06 external 4.096 V reference, fixed TGS channel mapping, and raw-code/voltage output only.

## Raw CSV Schema

`RawCsvLogger` writes schema version `1` with stable columns for monotonic and
wall-clock timestamps, system state, validity flags, error flags, TGS raw
codes/voltages, NH3/H2S raw codes and differential voltages, SGP41
`SRAW_VOC`/`SRAW_NOX`, BME690 fields/status, and SHT45 temperature/humidity.

The schema intentionally does not report NH3/H2S ppm, electrochemical current,
TGS resistance, TGS `Rs/R0`, TGS gas concentration, or odor classification as
valid outputs.

## Still Unresolved

- I2C adapter path or paths, such as `/dev/i2c-*`
- SPI device path for ADS114S06, such as `/dev/spidev*`
- GPIO chip identity/path and line offsets for ADS114S06 `DRDY#` and `START`
- Raspberry Pi physical header or BCM GPIO assignments
- ADS114S06 uses SPI mode 1; bit order, clock, PGA gain, data rate, filter, and conversion sequencing still need hardware validation.
- MCP3421 gain, resolution, and conversion mode
- Electrochemical polarity, zero offset, sensitivity, and calibration constants
- TGS heater control requirements and per-model calibration constants
- Raspberry Pi service user, device permissions, and deployment model
- BME690 compensated outputs are implemented against Bosch's BME690 SensorAPI
  logic, but still require Raspberry Pi and physical-board validation before
  they are treated as production-ready.

## Next Step

Confirm Raspberry Pi I2C/SPI/GPIO device paths and permissions without probing
unconfirmed sensor hardware. Keep hardware access disabled until the runtime
configuration can be validated safely.
