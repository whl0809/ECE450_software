# AGENTS.md

## 1. Purpose and Current Status

This repository contains the Raspberry Pi 5 software for an intelligent odor-sensing system.

Target platform:

- Raspberry Pi 5
- 64-bit Raspberry Pi OS
- Linux user-space application
- C++17 with CMake
- Linux I2C, `spidev`, and GPIO character-device APIs

This is not Arduino firmware. Use a normal `main()` and Linux/POSIX interfaces. Do not introduce PlatformIO, Arduino, ESP-IDF, `Wire`, `SPIClass`, FreeRTOS, wiringPi, deprecated GPIO sysfs, or direct `/dev/mem` access.

### Current software status

The sensor-acquisition scaffold is already implemented and host-validated:

- SHT45, SGP41, BME690, MCP3421, and ADS114S06 protocol logic exists.
- BME690 behavior follows Bosch's official BME690/BME69x SensorAPI flow.
- ADS114S06 register configuration follows the TI ADS114S06 datasheet and uses SPI mode 1.
- Mock-based driver tests exist.
- A stable versioned raw `SensorFrame` and CSV schema exist.
- Windows/MinGW CMake builds and tests have passed.

This does **not** mean the system is hardware-verified.

### Current development phase

The next phase is Raspberry Pi and physical-board integration:

1. Build and test natively on Raspberry Pi 5.
2. Finalize and document Pi physical-pin, BCM GPIO, `gpiochip`, and line-offset mappings.
3. Confirm `/dev/i2c-*` and `/dev/spidev*` paths and permissions.
4. Connect the two boards safely.
5. Validate each sensor and ADC on real hardware.
6. Tune provisional ADC and timing parameters from measurements.
7. Run stable multi-sensor CSV acquisition tests.

Do not redesign completed drivers or schemas unless a verified hardware result, official documentation, or demonstrated defect requires it.

---

## 2. Confirmed Sensor and Board Contract

The system contains 11 physical sensors.

### I2C devices

Use these confirmed 7-bit addresses:

| Device | Address |
|---|---:|
| SHT45-AD1F-R2 | `0x44` |
| SGP41-D-R4 | `0x59` |
| NH3 MCP3421A1T-E/CH | `0x69` |
| H2S MCP3421A2T-E/CH | `0x6A` |
| BME690 | `0x76` |

BME690 is in I2C mode because:

```text
CSB -> VDD3V3
SDO -> DGND
```

Board 1 and the Board 2 SHT45 provisionally share one configurable Raspberry Pi I2C adapter. The architecture must still permit per-board or per-device adapter overrides. This is a software assumption, not yet a hardware-validated wiring result.

### Mixed TGS array through ADS114S06

The six TGS sensors are distinct models:

```text
ADS114S06 AIN0 -> TGS2610_VOUT
ADS114S06 AIN1 -> TGS2620_VOUT
ADS114S06 AIN2 -> TGS2603_VOUT
ADS114S06 AIN3 -> TGS2602_VOUT
ADS114S06 AIN4 -> TGS2600_VOUT
ADS114S06 AIN5 -> TGS2611_VOUT
ADS114S06 AINCOM -> AGND
```

Treat each TGS model as a separate logical sensor with separate metadata, baseline, and later calibration.

The Raspberry Pi communicates with ADS114S06, not directly with the TGS sensors.

### Board 1 connector

Confirmed CN1 signals:

```text
CN1 pin 1 -> SENSOR_SCL
CN1 pin 2 -> DGND
CN1 pin 3 -> SENSOR_SDA
CN1 pin 4 -> DGND
CN1 pins 5 and 6 -> unconfirmed; do not use
```

Board 1 places SGP41, BME690, NH3 MCP3421, and H2S MCP3421 on `SENSOR_SDA` and `SENSOR_SCL`.

### Board 2 ADS114S06 connector

Confirmed CN1 signals:

```text
CN1 pin 1 -> START
CN1 pin 2 -> DIN   (Raspberry Pi MOSI)
CN1 pin 3 -> SCLK
CN1 pin 4 -> DOUT  (Raspberry Pi MISO)
CN1 pin 5 -> DRDY#
CN1 pin 6 -> AGND
```

Confirmed ADS114S06 board facts:

```text
CS#     -> DGND      (permanently selected)
RESET#  -> IOVDD     (no Raspberry Pi reset line)
CLK     -> DGND      (internal clock configuration)
AVDD    -> 5 V analog rail
DVDD    -> 3.3 V
IOVDD   -> 3.3 V
REFP0   -> external 4.096 V reference from REF5040AIDR
REFN0   -> AGND
```

The ADC is the sole active device on its physical SPI data lines. Do not add an ADS chip-select GPIO. A Raspberry Pi CE line may be needed only to expose a `spidev` node and may remain physically unconnected.

### Board 2 SHT45 pads

```text
Pad 1 -> SHT45_SDA
Pad 2 -> SHT45_SCL
Pad 3 -> VDD3V3
Pad 4 -> AGND
```

Under the current independent-power plan, connect SDA, SCL, and GND only. Do not connect Raspberry Pi 3.3 V to Pad 3 unless the power architecture is intentionally changed and revalidated.

### Electrochemical front ends

Provisional assembly assumption:

```text
NH3 JP2 = OPEN -> three-electrode sensor
H2S JP2 = OPEN -> three-electrode sensor
```

This must later be checked from the assembled boards, BOM, solder links, or continuity measurement.

Confirmed front-end facts:

```text
MCP3421 VIN+ -> TIA output
MCP3421 VIN- -> VBIAS
VBIAS nominal -> approximately 1.65 V
TIA feedback resistor -> 1 MOhm
TIA feedback capacitor -> 10 uF
TIA input series resistor -> 100 Ohm
Output low-pass resistor -> 49.9 kOhm
Output low-pass capacitor -> 10 uF
```

Do not fix the final electrochemical sign convention until validated with known gas exposure and actual sensor orientation.

### Power plan

```text
Raspberry Pi 5 -> independently powered
Board 1         -> independently powered through its USB-C input
Board 2         -> independently powered through its USB-C input
All three       -> common ground
```

Do not parallel Raspberry Pi 3.3 V or 5 V with either board's regulated rail. Before joining I2C domains, validate common ground, 3.3 V logic, effective pull-up resistance, and back-powering behavior when a board is off.

---

## 3. Safety and Information That Must Not Be Invented

Raspberry Pi GPIO uses 3.3 V logic. Never drive a 5 V signal into a Pi GPIO. Do not power sensors, heaters, pumps, valves, displays, or blowers directly from a GPIO.

Keep these identifiers distinct:

```text
Physical header pin
BCM GPIO number
Linux gpiochip identity
GPIO line offset
Application signal name
```

Never silently guess:

- Raspberry Pi physical-header pin assignments
- BCM GPIO assignments
- `/dev/gpiochip*` path or GPIO line offsets
- `/dev/i2c-*` adapter path
- `/dev/spidev*` device path
- START or DRDY# GPIO mapping and active-edge behavior
- final ADS114S06 clock, PGA, data rate, filter, settling, or sequencing
- final MCP3421 gain, resolution, or conversion mode
- electrochemical polarity, zero offset, calibrated sensitivity, or ppm conversion
- TGS heater control, `R0`, `Rs/R0`, or gas conversion
- actuator or display wiring
- Linux service user, permissions, groups, overlays, or udev rules

When required information is missing:

1. Keep it runtime-configurable.
2. Leave hardware access disabled when safe operation is not possible.
3. Add a clear TODO or error.
4. State what measurement, schematic, OS, or datasheet information is required.
5. Do not substitute a plausible-looking value.

Provisional defaults are allowed only when they are manufacturer-supported, clearly labeled, runtime-configurable, and not described as hardware-validated.

---

## 4. Provisional Runtime Profile

These defaults support initial integration and may change after real measurements.

### Shared I2C

- Default to one named, configurable adapter for Board 1 and SHT45.
- Keep the adapter path unset or disabled until explicitly configured on the Pi.
- Serialize access when multiple drivers share an adapter.
- Permit per-device or per-board overrides.

### SHT45

- 1 Hz target cadence
- high-precision command
- heater off during normal acquisition
- CRC required
- retain temperature and relative humidity

### SGP41

- 1 Hz command cadence
- use sufficiently recent valid SHT45 compensation
- retain `SRAW_VOC` and `SRAW_NOX`
- keep VOC/NOx indices separate from raw values
- report conditioning and stale-compensation states explicitly

### BME690

- I2C address `0x76`
- manufacturer-supported forced-measurement flow
- configurable oversampling, filter, heater temperature, heater duration, and interval
- retain temperature, humidity, pressure, gas resistance, gas-valid, and heater-stable status

### MCP3421

Initial example profile:

```text
resolution -> 16 bit
PGA gain   -> x1
mode       -> one-shot
```

Keep all three runtime-configurable. Retain signed raw code and differential voltage only. Do not report sensor current or ppm as valid.

### ADS114S06

Confirmed and initial settings:

```text
reference  -> external 4.096 V
SPI mode   -> mode 1
PGA        -> x1 provisional
SPI clock  -> 1 MHz provisional
channels   -> confirmed AIN0-AIN5 mapping
```

Keep clock, PGA, data rate, digital filter, DRDY use, channel settling, and conversion sequencing configurable. Retain signed raw code and ADC input voltage only.

---

## 5. Software Architecture and Driver Rules

Preserve the existing separation of responsibilities:

- `hardware/linux/`: Linux file descriptors, I2C, SPI, and GPIO access
- `hardware/mock/`: deterministic test doubles
- `drivers/`: manufacturer command, register, CRC, status, and compensation logic
- `sensors/`: later physical conversion and calibration logic
- `services/`: scheduling, coordination, logging, and application lifecycle
- runtime configuration: device paths, GPIO mappings, and tunable settings
- `sensor_types.h`: shared measurement structures

Drivers must:

- accept explicit bus/device abstractions
- return explicit success, status, and error information
- check all transaction results
- implement bounded timeouts
- preserve raw measurements
- distinguish not-ready, invalid, stale, saturated, and communication-failed states
- remain mock-testable
- avoid blocking forever

Drivers must not:

- open arbitrary device paths internally
- print routine output directly
- call `exit()`
- assume root access
- fabricate values when hardware is unavailable
- replace invalid readings with zero
- contain project calibration constants
- implement display, actuator, experiment-label, or ML behavior

Use official manufacturer documentation as the source of truth for commands, registers, CRC, timing, compensation, and transfer equations. Do not invent register values.

Source precedence:

1. final schematic and PCB/BOM
2. verified Pi-to-board wiring document
3. manufacturer datasheet or official SensorAPI/reference code
4. Raspberry Pi and Linux kernel documentation
5. existing hardware-verified project code

Stop and report conflicts instead of guessing.

---

## 6. Data, Scheduling, and Logging Contract

The existing versioned raw `SensorFrame` and CSV schema are part of the acquisition contract. Preserve stable field meaning and order within a schema version.

Required data categories:

- schema version
- monotonic timing and wall-clock timestamp
- system state
- six TGS signed raw codes and ADC voltages in this fixed order:
  - TGS2610
  - TGS2620
  - TGS2603
  - TGS2602
  - TGS2600
  - TGS2611
- NH3 and H2S signed MCP3421 raw codes and differential voltages
- SGP41 `SRAW_VOC` and `SRAW_NOX`
- BME690 temperature, humidity, pressure, gas resistance, gas-valid, and heater-stable
- SHT45 temperature and humidity
- per-sensor timestamps or age/freshness information
- validity and error flags

Do not present these as valid until calibration is completed:

```text
TGS resistance or Rs/R0
TGS gas concentration
NH3 or H2S sensor current
NH3 or H2S ppm
odor classification
```

Use `std::chrono::steady_clock` for scheduling and deadlines. Use wall-clock time for files and experiment records. Do not assume all sensors update at the same rate.

Use edge events, `poll()`, or bounded polling for DRDY. Avoid busy loops and unbounded sleeps.

A single sensor failure should normally produce an invalid/error state without freezing the complete acquisition system.

CSV requirements:

- stable column order per schema version
- explicit timestamps, validity, and errors
- no fabricated zero values
- diagnostics separate from measurement rows
- configurable output path and enable flag
- clear failure when the output file cannot be opened
- safe flush and close on shutdown

Production log rotation, disk quotas, crash recovery, and abrupt-power-loss guarantees remain deferred.

---

## 7. Raspberry Pi Integration and Validation

### Native build

Use:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Do not claim hardware verification from compilation or mock tests.

### Hardware integration order

Validate in this order:

1. Raspberry Pi native build and mock tests
2. I2C/SPI/GPIO device discovery and permissions
3. common-ground and signal-voltage checks
4. Board 1 I2C devices
5. Board 2 SHT45
6. NH3 and H2S MCP3421 readings
7. ADS114S06 SPI, START, DRDY#, and register readback
8. six TGS channels
9. integrated SensorManager and CSV logging
10. extended stability and fault tests

Expected I2C addresses on the provisional shared bus:

```text
0x44  SHT45
0x59  SGP41
0x69  NH3 MCP3421
0x6A  H2S MCP3421
0x76  BME690
```

Do not repeatedly scan I2C during normal acquisition and do not probe unknown addresses in production mode.

### Required real-hardware checks

Confirm and record:

- actual `/dev/i2c-*`, `/dev/spidev*`, and `/dev/gpiochip*`
- physical-header, BCM GPIO, chip, and line-offset mapping
- permissions and required packages
- SPI mode 1 and stable clock
- ADS register readback
- actual DRDY# active edge and timeout behavior
- START behavior and selected conversion sequence
- channel settling after mux changes
- ADC saturation and noise
- appropriate ADS PGA, data rate, and filter
- appropriate MCP3421 mode, resolution, and gain
- I2C pull-up and back-powering behavior
- behavior when a sensor is disconnected or unpowered
- clean shutdown and CSV integrity

Recommended acceptance tests:

- all devices initialize and provide plausible nonconstant readings
- every sensor failure maps to explicit validity/error fields
- 30-60 minutes of complete acquisition without deadlock or corrupted CSV
- one disconnect/failure test
- one normal `SIGINT`/`SIGTERM` shutdown test

Raw data acquisition is hardware-complete only after these checks pass. Calibration and odor classification remain separate later phases.

Do not edit boot overlays, user groups, udev rules, services, or system configuration without explicit permission. Document required commands instead.

---

## 8. Testing, Coding, and Change Rules

Use RAII for Linux resources. Use fixed-width integer types where data width matters. Include units in names such as `VoltageV`, `PressurePa`, `TimeoutMs`, and `ResistanceOhm`. Keep functions focused and compile with warnings enabled.

Mock/unit tests should cover applicable cases including:

- command and register encoding
- CRC success and failure
- byte order and signed conversion
- ready/not-ready behavior
- timeout and communication failure
- register mismatch
- saturation or invalid status
- stale compensation and stale sensor data
- missing hardware configuration
- partial sensor failure
- raw-to-voltage calculations

Hardware tests must be separate from ordinary unit tests and must identify the target adapter/device explicitly.

After changes:

1. run supported CMake configure, build, and tests;
2. report changed files and behavior;
3. distinguish host/mock validation from Pi and hardware validation;
4. state assumptions and remaining hardware TODOs;
5. do not commit automatically unless explicitly requested.

---

## 9. Deferred Scope

Do not implement these unless explicitly requested and required information is available:

- calibrated NH3/H2S ppm
- final electrochemical polarity and zero correction
- TGS `R0`, `Rs/R0`, or gas concentration
- odor classification or ML inference
- pump, valve, blower, or heater control
- display or touch integration
- production systemd deployment
- production-grade storage management

Sensor acquisition must remain independent of future display, actuator, and ML components.
