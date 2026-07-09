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

This specific Raspberry Pi 5 has now been inspected. The initial Linux device paths, GPIO controller, and Pi-to-board signal assignments are selected and may be used in a machine-specific runtime configuration.

Pi-native diagnostic execution has occurred with only the ADS114S06/TGS PCB connected. The observed ADS diagnostic result before the current debug-instrumentation update was:

```text
diagnostic,ads114s06_begin,ok=false,error_flags=10240
diagnostic,ads114s06_read_tgs_array,ok=false,error_flags=10241
```

At that time, Linux SPI transfers reported success, but ADS114S06 initialization stopped during register readback verification in `ADS114S06Driver::writeRegisterChecked()`. This is an observed diagnostic failure, not a demonstrated root cause. Updated diagnostics must be rerun on the Raspberry Pi with the connected PCB before ADS communication, START/DRDY behavior, or six-channel TGS acquisition can be considered hardware-validated.

The next phase is native build and physical-board validation:

1. Add or update the machine-specific runtime configuration with the selected paths and GPIO mappings in Section 3.
2. Build and run mock tests natively on Raspberry Pi 5.
3. Connect both boards using the documented wiring.
4. Confirm the five expected I2C addresses on `/dev/i2c-1`.
5. Validate ADS114S06 SPI, START, DRDY#, register readback, and six-channel acquisition.
6. Tune provisional ADC and timing parameters from real measurements.
7. Run stable multi-sensor CSV acquisition and fault tests.

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

For this Raspberry Pi integration, Board 1 and the Board 2 SHT45 are assigned to the standard header I2C bus at `/dev/i2c-1`, using BCM GPIO2 for SDA and BCM GPIO3 for SCL. The architecture must still permit per-board or per-device adapter overrides. The selected bus and wiring remain subject to connected-board validation.

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

## 3. Selected Raspberry Pi Integration Profile

The following values were observed or selected for the current Raspberry Pi 5 Model B Rev 1.1 running 64-bit Debian 12. Store them in a machine-specific runtime configuration. Do not bury them inside low-level drivers.

### Linux resources

```text
shared I2C adapter -> /dev/i2c-1
ADS SPI device     -> /dev/spidev0.0
SPI mode           -> mode 1
SPI clock          -> 1 MHz provisional
GPIO controller    -> /dev/gpiochip4
expected GPIO label-> pinctrl-rp1
START line offset  -> 17, active high
DRDY# line offset  -> 27, active low
```

The current user is already a member of the `i2c`, `spi`, and `gpio` groups. Do not modify user groups unless a later permission check demonstrates a problem.

### Physical-header and BCM assignments

| Function | Physical pin | BCM GPIO | Board connection |
|---|---:|---:|---|
| Shared I2C SDA | 3 | GPIO2 | Board 1 CN1 pin 3 and Board 2 SHT45 pad 1 |
| Shared I2C SCL | 5 | GPIO3 | Board 1 CN1 pin 1 and Board 2 SHT45 pad 2 |
| Shared I2C ground | 6 | GND | Board 1 CN1 pin 2 or 4 and Board 2 SHT45 pad 4 |
| ADS MOSI | 19 | GPIO10 | Board 2 CN1 pin 2, DIN |
| ADS MISO | 21 | GPIO9 | Board 2 CN1 pin 4, DOUT |
| ADS SCLK | 23 | GPIO11 | Board 2 CN1 pin 3, SCLK |
| ADS START | 11 | GPIO17 | Board 2 CN1 pin 1, START |
| ADS DRDY# | 13 | GPIO27 | Board 2 CN1 pin 5, DRDY# |
| ADS ground | 20 | GND | Board 2 CN1 pin 6, AGND |

Raspberry Pi CE0 at physical pin 24 is not connected to the board. `/dev/spidev0.0` may still toggle CE0 internally, but the ADS114S06 `CS#` is permanently tied low on Board 2.

These assignments finalize the initial software integration configuration. They are not yet proof of electrical or protocol correctness; connected-board tests must still verify bus visibility, pinmux, signal behavior, and stable acquisition.

---

## 4. Safety and Information That Must Not Be Invented

Raspberry Pi GPIO uses 3.3 V logic. Never drive a 5 V signal into a Pi GPIO. Do not power sensors, heaters, pumps, valves, displays, or blowers directly from a GPIO.

Keep these identifiers distinct:

```text
Physical header pin
BCM GPIO number
Linux gpiochip identity
GPIO line offset
Application signal name
```

The machine-specific paths and mappings in Section 3 are selected for this Pi and may be used. Do not silently change them, substitute different buses or pins, or describe them as hardware-verified without new evidence.

Never silently guess:

- final ADS114S06 clock, PGA, data rate, digital filter, channel settling, or conversion sequence
- final START behavior or measured DRDY# edge/timing behavior
- final MCP3421 gain, resolution, or conversion mode
- electrochemical polarity, zero offset, calibrated sensitivity, or ppm conversion
- TGS heater behavior, `R0`, `Rs/R0`, or gas conversion
- actuator or display wiring
- boot overlays, udev rules, system services, or privilege changes not explicitly requested

When required information is missing:

1. Keep it runtime-configurable.
2. Leave hardware access disabled when safe operation is not possible.
3. Add a clear TODO or error.
4. State what measurement, schematic, OS, or datasheet information is required.
5. Do not substitute a plausible-looking value.

Provisional defaults are allowed only when they are manufacturer-supported, clearly labeled, runtime-configurable, and not described as hardware-validated.

---

## 5. Provisional Runtime Profile

These defaults support initial integration and may change after real measurements.

### Shared I2C

- The machine-specific configuration should use `/dev/i2c-1` for Board 1 and SHT45.
- The generic example configuration should keep machine-specific paths unset or clearly marked as examples.
- Serialize access when multiple drivers share the adapter.
- Permit per-device or per-board overrides.
- Do not treat the bus as hardware-validated until the connected scan shows `0x44`, `0x59`, `0x69`, `0x6A`, and `0x76`.
- Keep these machine-specific values in a local TOML file; preserve a portable generic example configuration.

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
reference   -> external 4.096 V
SPI device  -> /dev/spidev0.0 for this Pi
SPI mode    -> mode 1
SPI clock   -> 1 MHz provisional
GPIO chip   -> /dev/gpiochip4, expected label pinctrl-rp1
START       -> line 17, active high
DRDY#       -> line 27, active low
PGA         -> x1 provisional
channels    -> confirmed AIN0-AIN5 mapping
```

Keep clock, PGA, data rate, digital filter, DRDY use, channel settling, and conversion sequencing configurable. Retain signed raw code and ADC input voltage only.

---

## 6. Software Architecture and Driver Rules

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

## 7. Data, Scheduling, and Logging Contract

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

## 8. Raspberry Pi Integration and Validation

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

1. Create the machine-specific runtime configuration from Section 3.
2. Run the Raspberry Pi native build and mock tests.
3. Recheck device nodes, GPIO label, pinmux, and permissions.
4. With all systems unpowered, complete the documented wiring and common-ground connection.
5. Power the Pi and both independently powered boards; verify no unintended rail connection or back-powering.
6. Scan `/dev/i2c-1` and confirm all five expected addresses.
7. Validate SHT45, SGP41, BME690, and both MCP3421 devices individually.
8. Validate ADS114S06 SPI, START, DRDY#, register readback, and six TGS channels.
9. Run integrated SensorManager and CSV logging.
10. Run extended stability, disconnect, and clean-shutdown tests.

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

The selected paths and mappings are documented in Section 3. On the target Pi, confirm that they still exist after boot and then verify:

- `/dev/i2c-1`, `/dev/spidev0.0`, and `/dev/gpiochip4` are accessible
- `/dev/gpiochip4` has label `pinctrl-rp1`
- GPIO2/3 are muxed for I2C and GPIO9/10/11 for SPI0
- GPIO17 and GPIO27 are available before the application requests them
- the current user can access I2C, SPI, and GPIO without running the application as root
- SPI mode 1 and a stable initial clock
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

## 9. Testing, Coding, and Change Rules

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

## 10. Deferred Scope

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
