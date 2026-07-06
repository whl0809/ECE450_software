# AGENTS.md

## Project Overview

This repository contains the Raspberry Pi 5 software for an intelligent odor-sensing system.

The target controller is:

- Raspberry Pi 5
- Raspberry Pi OS, 64-bit
- Linux user-space application

The project is developed with:

- VS Code
- CMake
- C++17
- GCC or Clang on Raspberry Pi OS
- Linux I2C, SPI, and GPIO user-space interfaces

This repository is the Raspberry Pi 5 version of an existing ESP32-S3 project. The sensor electronics and confirmed sensor assignments remain unchanged unless a later hardware decision explicitly changes them.

The system contains four functional areas:

1. Sensor interface
2. Raspberry Pi 5 controller
3. Display
4. Power

The current priority is reliable sensor data acquisition together with a stable, versioned raw-data output format and basic CSV logging. Display integration, airflow control, production-grade log rotation and crash recovery, system-service deployment, and machine-learning inference will be integrated later.

This is not Arduino firmware. The application runs as a normal Linux process with a `main()` function.

---

## Raspberry Pi 5 Platform Architecture

The Raspberry Pi 5 is a Linux single-board computer rather than a microcontroller.

The software communicates with external hardware through Linux device interfaces:

```text
I2C devices
    -> Linux I2C adapter
    -> /dev/i2c-*
    -> I2C bus wrapper
    -> sensor drivers

SPI devices
    -> Linux SPI controller
    -> /dev/spidev*
    -> SPI device wrapper
    -> ADS114S06 driver

GPIO control and edge events
    -> Linux GPIO character device
    -> /dev/gpiochip*
    -> GPIO line wrapper
    -> DRDY, RESET, START, pump, valve, and other control signals
```

Use Linux subsystem APIs for I2C and SPI rather than bit-banging them through GPIO.

Use the GPIO character-device API through `libgpiod` or an equivalent thin project wrapper. Do not use obsolete `sysfs` GPIO. Do not use direct `/dev/mem` register access.

Do not assume that Linux bus or GPIO device numbers are stable between systems or boots. Device paths and line mappings must be configurable and validated at startup.

The Raspberry Pi 5 does not provide general-purpose analog inputs. All analog sensors must continue to be measured through the external ADC signal chains described below.

---

## Current Sensor Architecture

The system contains 11 physical sensors.

The following hardware decisions are confirmed:

- NH3 uses MCP3421A1T-E/CH at 7-bit I2C address `0x69`.
- H2S uses MCP3421A2T-E/CH at 7-bit I2C address `0x6A`.
- VOC and NOx sensing uses `SGP41-D-R4` at fixed 7-bit I2C address `0x59`.
- Temperature and humidity sensing uses `SHT45-AD1F-R2` at fixed 7-bit I2C address `0x44`.
- BME690 communicates through I2C at confirmed 7-bit address `0x76` because `CSB` is tied to `VDD3V3` and `SDO` is tied to `DGND`.
- The TGS array contains six different Figaro sensor models measured through one ADS114S06 ADC using standard SPI.
- The electrochemical sensors are provisionally configured as three-electrode sensors: NH3 JP2 open and H2S JP2 open. This is an implementation assumption that must later be verified against the assembled boards or BOM.
- Board 1 and the Board 2 SHT45 provisionally share one Raspberry Pi I2C adapter. All confirmed addresses are unique. The shared adapter path remains runtime-configurable, and the architecture must still permit a per-board or per-device adapter override if later electrical validation requires separate buses. This is a software integration assumption, not yet a hardware-validated wiring result.

### Analog gas sensors

#### Mixed TGS sensor array

The array contains six different sensors, not six identical TGS2610 devices:

```text
ADS114S06 AIN0 -> TGS2610_VOUT
ADS114S06 AIN1 -> TGS2620_VOUT
ADS114S06 AIN2 -> TGS2603_VOUT
ADS114S06 AIN3 -> TGS2602_VOUT
ADS114S06 AIN4 -> TGS2600_VOUT
ADS114S06 AIN5 -> TGS2611_VOUT
ADS114S06 AINCOM -> AGND
```

Each TGS channel produces an analog signal through a 5 V sensing circuit, approximately 10 kOhm load resistance, unity-gain OPA2192 buffering, and a small series output resistor before the ADC input.

Data path:

```text
TGS2610 / TGS2620 / TGS2603 / TGS2602 / TGS2600 / TGS2611
    -> analog sensing circuits
    -> ADS114S06
    -> standard SPI
    -> Raspberry Pi 5
```

The Raspberry Pi communicates with ADS114S06, not directly with the TGS sensors. Treat every TGS model as a separate logical sensor with separate calibration, baseline, and metadata.

#### NH3 sensor

- One EC Sense ES1-NH3-100 sensor
- Its signal passes through a dedicated analog front end
- The analog-front-end output is measured by MCP3421A1T-E/CH
- Its confirmed 7-bit I2C address is `0x69`

Data path:

```text
ES1-NH3-100
    -> NH3 analog front end
    -> MCP3421A1T-E/CH at 0x69
    -> I2C
    -> Raspberry Pi 5
```

#### H2S sensor

- One EC Sense ES1-H2S-100 sensor
- Its signal passes through a dedicated analog front end
- The analog-front-end output is measured by MCP3421A2T-E/CH
- Its confirmed 7-bit I2C address is `0x6A`

Data path:

```text
ES1-H2S-100
    -> H2S analog front end
    -> MCP3421A2T-E/CH at 0x6A
    -> I2C
    -> Raspberry Pi 5
```

The two MCP3421 devices have distinct factory-programmed addresses and may share one I2C bus. Their Linux I2C adapter path must still follow the actual wiring and operating-system configuration.

### Digital sensors

#### SGP41

- Exact part: `SGP41-D-R4`
- Communicates through I2C at fixed 7-bit address `0x59`
- Provides raw VOC and NOx signals
- Use valid, sufficiently recent SHT45 measurements for temperature and humidity compensation when required by the official API
- Preserve raw VOC and NOx values
- Store derived VOC Index and NOx Index separately from raw values
- Keep the intended algorithm sampling interval stable

#### BME690

- Communicates through I2C, not SPI
- Confirmed 7-bit I2C address: `0x76`
- Hardware mode selection: `CSB -> VDD3V3`, `SDO -> DGND`
- Measures gas resistance, temperature, humidity, and pressure
- Use an I2C-based driver configuration
- Do not include SPI-mode BME690 application logic
- Preserve gas resistance and measurement status, including gas-valid and heater-stable information where available

#### SHT45

- Exact part: `SHT45-AD1F-R2`
- Communicates through I2C at fixed 7-bit address `0x44`
- Belongs to the Sensirion SHT4x family
- Measures temperature and relative humidity
- Uses the SHT4x command set, not SHT3x/SHT85 commands
- The integrated PTFE membrane does not change the software protocol
- CRC-check returned temperature and humidity data
- Use its measurements for SGP41 compensation, environmental monitoring, normalization, and later machine-learning inputs
- Initial target sampling interval is approximately one second

---

## Confirmed Board-Level Hardware Contract

### Board 1: digital and electrochemical sensor board

The confirmed CN1 signals are:

```text
CN1 pin 1 -> SENSOR_SCL
CN1 pin 2 -> DGND
CN1 pin 3 -> SENSOR_SDA
CN1 pin 4 -> DGND
CN1 pins 5 and 6 -> not confirmed; do not use until verified
```

Board 1 shares `SENSOR_SDA` and `SENSOR_SCL` among:

```text
SGP41           0x59
NH3 MCP3421     0x69
H2S MCP3421     0x6A
BME690          0x76
```

The board includes approximately 10 kOhm pull-ups from `SENSOR_SDA` and `SENSOR_SCL` to `SEN_VCC3V3`.

### Board 2: TGS/ADS114S06/SHT45 board

The ADS114S06 connector is confirmed as:

```text
CN1 pin 1 -> START
CN1 pin 2 -> DIN   (Raspberry Pi MOSI)
CN1 pin 3 -> SCLK
CN1 pin 4 -> DOUT  (Raspberry Pi MISO)
CN1 pin 5 -> DRDY#
CN1 pin 6 -> AGND
```

The ADS114S06 board-level connections are confirmed as:

```text
CS#     -> DGND      (permanently selected)
RESET#  -> IOVDD     (not controlled by Raspberry Pi)
CLK     -> DGND      (internal clock configuration)
AVDD    -> 5 V analog rail
DVDD    -> 3.3 V
IOVDD   -> 3.3 V
REFP0   -> external 4.096 V reference from REF5040AIDR
REFN0   -> AGND
```

Because `CS#` is permanently low, the ADS114S06 must be treated as the only active device on its physical SPI data lines. Do not design the Raspberry Pi version around a separate ADS chip-select GPIO unless the hardware is later revised. A Linux `spidev` node may still toggle an unconnected CE line; that CE line is not part of the board contract.

The SHT45 is exposed through confirmed pads:

```text
Pad 1 -> SHT45_SDA
Pad 2 -> SHT45_SCL
Pad 3 -> VDD3V3
Pad 4 -> AGND
```

Board 2 includes approximately 10 kOhm pull-ups from SHT45 SDA and SCL to 3.3 V. Under the current independent-power plan, connect Raspberry Pi SDA, SCL, and GND only. Do not connect the Raspberry Pi 3.3 V rail to Pad 3 unless the power architecture is intentionally changed and revalidated.

### Electrochemical analog front ends

Current provisional assembly assumption:

```text
NH3 JP2 = OPEN -> three-electrode sensor
H2S JP2 = OPEN -> three-electrode sensor
```

Treat this as provisional rather than hardware-verified. The final assembled state must later be checked from the BOM, solder links, or continuity measurement.

Both NH3 and H2S signal chains use OPA2192 analog front ends and MCP3421 differential measurements:

```text
MCP3421 VIN+ -> TIA output
MCP3421 VIN- -> VBIAS
VBIAS nominal -> approximately 1.65 V from a 10 kOhm / 10 kOhm divider
TIA feedback resistor -> 1 MOhm
TIA feedback capacitor -> 10 uF
TIA input series resistor -> 100 Ohm
Output low-pass resistor -> 49.9 kOhm
Output low-pass capacitor -> 10 uF
```

The magnitude relation may use approximately `|I_sensor| = |V_TIA - VBIAS| / 1 MOhm`, but the digital sign convention must remain configurable until validated with known gas exposure and the final sensor orientation. Do not report concentration as valid until sensitivity, polarity, zero offset, and calibration have been validated.

### Power architecture

The current integration plan is:

```text
Raspberry Pi 5 -> independently powered
Board 1         -> independently powered through its own USB-C input
Board 2         -> independently powered through its own USB-C input
All three systems -> common ground
```

Do not parallel the Raspberry Pi 3.3 V or 5 V rails with either board's regulated supply. Connect grounds and signal lines only unless a later reviewed power design explicitly changes this rule. Evaluate combined I2C pull-up resistance and possible back-powering before joining independently powered I2C domains.

---

## Electrical and Raspberry Pi Safety Rules

Raspberry Pi GPIO signals are 3.3 V logic. Do not assume that any external board signal is electrically compatible.

Before connecting or driving a signal, confirm from the schematic:

- signal voltage level
- direction
- required pull-up or pull-down
- whether level shifting is present
- whether the signal is open-drain
- whether a line is already driven by another device
- common-ground connection

Never drive a 5 V signal directly into a Raspberry Pi GPIO.

Do not power pumps, valves, heaters, blowers, displays, or other power loads directly from Raspberry Pi GPIO. GPIO may control a suitable driver or enable input only.

Do not assume the Raspberry Pi 5 can power the complete sensor interface from the 3.3 V header rail. Power budgeting and rail selection must follow the final power design.

Do not use Raspberry Pi physical header pin numbers interchangeably with BCM GPIO numbers or Linux GPIO line offsets.

Keep these identifiers distinct:

```text
Physical header pin
BCM GPIO name/number
Linux gpiochip identity
GPIO line offset
Application signal name
```

---

## Hardware Information That Must Not Be Invented

Never guess or silently assign any of the following:

- Raspberry Pi physical header pin mapping
- BCM GPIO numbers
- Linux GPIO chip path or chip index
- Linux GPIO line offsets
- I2C adapter path, such as `/dev/i2c-*`
- SPI device path, such as `/dev/spidev*`
- Linux SPI device path and any unconnected Raspberry Pi CE assignment used only to expose the controller
- Raspberry Pi physical-header and BCM-GPIO assignment for ADS114S06 SCLK, MOSI, MISO, START, and DRDY
- I2C wiring to the Raspberry Pi header or an intermediate connector
- final hardware-validated ADS114S06 PGA gain, data rate, filter, SPI timing, or conversion sequencing
- final hardware-validated MCP3421 gain, resolution, or conversion mode
- electrochemical analog-front-end polarity and final transfer-function sign
- TGS heater control parameters
- NH3 or H2S per-device calibrated sensitivity and zero-offset values
- current-to-concentration conversion constants
- signal voltage compatibility or level-shifter presence
- Raspberry Pi service user, permissions, or device-access groups
- display controller, display interface, or touch-controller model

The following I2C addresses are confirmed and may be used:

```text
SHT45 = 0x44
SGP41 = 0x59
NH3 MCP3421 = 0x69
H2S MCP3421 = 0x6A
BME690 = 0x76
```

### Provisional software defaults

Provisional operating defaults may be selected for initial software integration when all of the following are true:

- the value or mode is supported by the official manufacturer datasheet or reference driver;
- it is clearly labeled as provisional and not hardware-validated;
- it remains runtime-configurable rather than being buried in driver source code;
- startup validation rejects unsupported or internally inconsistent combinations;
- it can be changed without modifying the low-level driver implementation;
- missing Linux device paths or GPIO mappings keep physical hardware access disabled;
- raw values, status, and validity remain available so later tuning does not require redesigning the data path.

Do not confuse a provisional software default with a confirmed board-level hardware fact or a final experimentally validated setting.

When required information is missing:

1. Add a clearly named placeholder or TODO.
2. Keep the project buildable when practical.
3. Keep hardware access disabled if an unsafe or invalid configuration would otherwise be used.
4. Explain what schematic, BOM, operating-system, datasheet, or measurement information is required.
5. Do not substitute an undocumented numerical value or device path.

---

## Initial Software Operating Profile

These settings define the first software-integration profile. They are provisional, runtime-configurable, and not yet hardware-validated.

### Shared I2C integration

- Board 1 and the Board 2 SHT45 provisionally use one named Raspberry Pi I2C adapter configuration.
- The adapter device path must remain empty or disabled until it is explicitly configured on the target Raspberry Pi.
- All five confirmed addresses are unique: `0x44`, `0x59`, `0x69`, `0x6A`, and `0x76`.
- Permit a per-board or per-device adapter override without changing driver code.
- Serialize access when multiple drivers share one adapter.

### SHT45

- target sampling interval: one second
- high-precision measurement command
- heater disabled during normal acquisition
- CRC validation required
- preserve temperature and relative humidity
- reject invalid CRC data rather than substituting zero

### SGP41

- target command cadence: one second
- use sufficiently recent valid SHT45 temperature and humidity for compensation
- retain `SRAW_VOC` and `SRAW_NOX`
- expose conditioning-incomplete, stale-compensation, timeout, and CRC errors explicitly
- keep derived VOC Index and NOx Index separate from the raw values

### BME690

- use I2C address `0x76`
- use one simple manufacturer-documented forced-measurement profile initially
- preserve temperature, humidity, pressure, gas resistance, gas-valid, and heater-stable outputs
- keep oversampling, IIR filtering, heater temperature, heater duration, and measurement interval configurable
- label the example profile as provisional rather than final or optimized

### MCP3421

- implement all required configuration encoding and decoding for supported resolution, PGA gain, and one-shot or continuous conversion modes
- place the selected initial resolution, gain, and conversion mode in runtime configuration
- retain signed raw code and differential input voltage
- expose conversion-ready and timeout status
- do not apply a fixed electrochemical polarity assumption
- do not report sensor current or ppm as valid yet

### ADS114S06

- use the confirmed external `4.096 V` reference and confirmed AIN0-AIN5 TGS mapping
- implement command, register, multiplexer, conversion-ready, signed-code, and voltage-conversion logic
- place SPI mode, maximum clock, PGA, data rate, filter, and conversion sequencing in runtime configuration
- represent `CS#` as permanently asserted at board level; do not add an ADC chip-select GPIO
- retain signed raw code and ADC input voltage
- do not calculate TGS resistance, `Rs/R0`, or gas concentration yet

Hardware access must remain disabled when the required device paths or GPIO mappings are missing.

## Source-of-Truth Rules

Use the following precedence when implementing hardware behavior:

1. Final project schematic
2. Final PCB net names and bill of materials
3. Raspberry Pi connection diagram and cable/adapter documentation
4. Manufacturer datasheet
5. Manufacturer reference driver or official library
6. Raspberry Pi and Linux kernel documentation
7. Existing hardware-verified project code

Do not invent register values.

Do not rely on an unofficial blog when official documentation is available.

When implementing register-level behavior, document the relevant datasheet command or register name in code comments.

If repository documentation conflicts with the final schematic, stop and report the conflict rather than guessing.

---

## Software Architecture

Keep the application modular and testable without physical hardware.

Recommended project structure:

```text
.
├── AGENTS.md
├── CMakeLists.txt
├── README.md
├── config/
│   └── odor-sensing.example.toml
│
├── include/
│   ├── config.h
│   ├── sensor_types.h
│   ├── error_flags.h
│   ├── hardware/
│   │   ├── II2CBus.h
│   │   ├── ISPIDevice.h
│   │   └── IGpioLine.h
│   ├── drivers/
│   │   ├── ADS114S06Driver.h
│   │   ├── MCP3421Driver.h
│   │   ├── SHT45Driver.h
│   │   ├── SGP41Driver.h
│   │   └── BME690Driver.h
│   ├── sensors/
│   │   ├── TGSSensorArray.h
│   │   └── ElectrochemicalSensor.h
│   └── services/
│       ├── SensorManager.h
│       ├── DataLogger.h
│       └── CalibrationManager.h
│
├── src/
│   ├── main.cpp
│   ├── hardware/linux/
│   │   ├── LinuxI2CBus.cpp
│   │   ├── LinuxSPIDevice.cpp
│   │   └── LinuxGpioLine.cpp
│   ├── drivers/
│   ├── sensors/
│   └── services/
│
├── tests/
│   ├── mocks/
│   │   ├── MockI2CBus.h
│   │   ├── MockSPIDevice.h
│   │   └── MockGpioLine.h
│   └── unit/
│
└── scripts/
    ├── inspect_i2c.sh
    └── inspect_spi.sh
```

The exact structure may be refined, but preserve these separations:

- Linux device access belongs in `hardware/linux/`.
- Device command and register logic belongs in `drivers/`.
- Physical conversion and calibration logic belongs in `sensors/`.
- Scheduling and coordination belong in `services/`.
- Runtime hardware paths and mappings belong in configuration, not buried in drivers.
- Shared measurement structures belong in `sensor_types.h`.
- Calibration values must not be buried inside low-level drivers.
- Unit tests should use mock bus and GPIO interfaces.

Do not create an Arduino-compatible `setup()`/`loop()` layer. Use a normal `main()` and explicit application lifecycle.

---

## Linux Hardware Abstraction

### I2C

Use a project-level I2C abstraction around the Linux I2C userspace interface.

An I2C object should represent a Linux adapter and support operations needed by the sensor drivers, such as:

- opening and closing an adapter device
- selecting a 7-bit slave address
- writing command bytes
- reading response bytes
- combined write/read transactions when required
- returning explicit error information

Do not let individual drivers silently open arbitrary `/dev/i2c-*` paths.

Do not hardcode an I2C adapter number without confirmed system configuration. Adapter numbers can vary. Make the adapter path configurable and validate it during startup.

The initial diagnostic workflow should use operating-system tools to list adapters and scan only the confirmed sensor bus.

### SPI

Use a project-level SPI abstraction around Linux `spidev`.

The ADS114S06 SPI device configuration must explicitly include:

- device path
- SPI mode
- bits per word
- maximum clock frequency
- board-level always-selected behavior

Do not assume the default `spidev` settings are correct.

The ADS114S06 `CS#` pin is tied permanently to `DGND`; there is no Raspberry Pi-controlled ADC chip-select line in the current board design. Treat the ADC as the sole device on its physical SPI data lines. Do not invent a GPIO chip-select. If a Linux SPI controller exposes a CE line only to create the `spidev` node, that CE line may remain physically unconnected and must be documented as such.

### GPIO

Use the Linux GPIO character-device API through `libgpiod` or a thin wrapper.

GPIO code should support:

- requesting input and output lines
- initial output values
- active-high or active-low semantics
- input edge events
- event timeouts
- releasing lines cleanly

Do not hardcode `/dev/gpiochip4` or another gpiochip number solely because the target is Raspberry Pi 5. Resolve the intended GPIO controller by explicit configuration or verified chip label.

Do not use deprecated GPIO sysfs APIs.

Do not busy-poll DRDY or interrupt lines at maximum CPU usage. Use edge events, `poll()`, or bounded periodic polling where appropriate.

---

## Driver Responsibilities

Each hardware driver should:

- accept an explicit bus or device abstraction
- avoid owning unrelated global hardware resources
- provide `begin()` or equivalent initialization
- return explicit success or failure
- check all Linux system-call and bus-operation results
- expose raw measurements before project-specific conversion
- detect timeout conditions
- avoid blocking forever
- keep device-specific behavior separate from application behavior
- remain unit-testable using mock hardware interfaces

Drivers must not:

- print directly to stdout or stderr during routine operation
- terminate the application process
- call `exit()`
- control unrelated sensors
- store experiment labels
- implement display behavior
- implement machine-learning classification
- contain guessed calibration constants
- replace invalid readings with zero
- assume they run as root

Use negative `errno` values, `std::error_code`, or a clear project result type consistently. Do not mix several incompatible error styles without reason.

---

## Current Implementation Scope

The current software phase includes:

1. adding the provisional runtime operating profile to the example configuration;
2. implementing real device-protocol logic for SHT45, SGP41, BME690, MCP3421, and ADS114S06 through the existing Linux hardware abstractions;
3. adding CRC, timeout, readiness, sign-extension, validity, and error handling where applicable;
4. implementing non-blocking or bounded SensorManager scheduling for different sensor cadences;
5. implementing a stable, versioned raw `SensorFrame` and CSV schema;
6. adding or expanding mock-based tests for successful reads, CRC failures, timeouts, signed ADC values, stale data, and missing hardware configuration.

The expected status after this phase is:

```text
protocol implemented
mock-tested
compile-tested
not yet Raspberry Pi hardware-validated
```

A successful host build does not prove physical communication, GPIO mapping, electrical compatibility, timing margins, or measurement accuracy.

Drivers should be implemented in this order unless repository dependencies justify a small change:

1. SHT45
2. SGP41
3. BME690
4. MCP3421
5. ADS114S06
6. SensorManager integration and raw logging

## Sensor Data Requirements

Preserve raw data whenever possible.

A stable, versioned raw `SensorFrame` and CSV schema must be implemented during the current phase. The exact C++ layout may be refined, but it must contain fields equivalent to the following:

```cpp
struct SensorFrame {
    uint32_t schemaVersion;
    std::chrono::steady_clock::time_point monotonicTimestamp;
    std::chrono::system_clock::time_point wallTimestamp;
    SystemState systemState;

    // Fixed order: TGS2610, TGS2620, TGS2603, TGS2602, TGS2600, TGS2611.
    std::array<int32_t, 6> tgsAdcRaw;
    std::array<float, 6> tgsVoltageV;

    int32_t nh3AdcRaw;
    float nh3DifferentialVoltageV;

    int32_t h2sAdcRaw;
    float h2sDifferentialVoltageV;

    uint16_t sgp41RawVoc;
    uint16_t sgp41RawNox;

    float bme690TemperatureC;
    float bme690HumidityRh;
    float bme690PressurePa;
    float bme690GasResistanceOhm;
    bool bme690GasValid;
    bool bme690HeaterStable;

    float sht45TemperatureC;
    float sht45HumidityRh;

    SensorTimestamps sensorTimestamps;
    uint64_t validFlags;
    uint64_t errorFlags;
};
```

The current raw schema must not present the following as valid measurements:

```text
TGS resistance
TGS Rs/R0
TGS gas concentration
NH3 sensor current
NH3 ppm
H2S sensor current
H2S ppm
odor classification
```

These may be added later as separately named derived fields after the corresponding polarity, transfer function, calibration, and validation work is complete.

Use both monotonic and wall-clock time where useful:

- monotonic time for scheduling, deadlines, sample intervals, and data age
- wall-clock time for files and experiment records

Each sensor group must retain its own update timestamp or data age because sensors update at different rates.

Keep invalid, stale, unavailable, and valid-zero measurements distinguishable. Prefer `NAN`, validity flags, error flags, or typed result structures rather than numeric sentinel values that could be mistaken for real data.

Keep the CSV column order stable for a given schema version. Changing field meaning or order requires a schema-version change and documentation.

---

## Scheduling and Timing Rules

The Raspberry Pi 5 application runs under Linux and must not be treated as a hard real-time microcontroller.

Use:

- `std::chrono::steady_clock` for intervals and deadlines
- `std::this_thread::sleep_until()` for scheduled tasks where appropriate
- `poll()` or GPIO edge events for hardware-ready signals
- bounded worker threads only when they simplify the design
- explicit state machines for multi-step conversions

Avoid:

- CPU-intensive busy loops
- unbounded sleeps inside reusable drivers
- assuming exact microsecond scheduling from a normal Linux process
- using wall-clock time for interval calculations

Do not assume every sensor has the same sample rate.

Follow manufacturer timing requirements for:

- ADS114S06 conversion completion and channel settling
- MCP3421 conversion timing
- SGP41 conditioning and index-algorithm cadence
- BME690 heater operation
- SHT45 measurement completion

The application should continue running if one sensor fails, unless the failure creates an electrical or safety risk.

A failed sensor must produce an error state rather than freeze the complete acquisition system.

---

## I2C Rules

Confirmed addresses:

```text
SHT45 = 0x44
SGP41 = 0x59
NH3 MCP3421 = 0x69
H2S MCP3421 = 0x6A
BME690 = 0x76
```

- Use 7-bit addresses in application configuration.
- Keep every device associated with an explicit I2C adapter object.
- Check for address conflicts before integration.
- Use timeouts where possible.
- Check CRC where the sensor protocol provides it.
- Do not repeatedly scan the entire bus during normal acquisition.
- Do not probe unknown addresses in production mode.
- Avoid simultaneous unsynchronized access to one adapter from multiple threads.
- If multiple threads use one adapter, serialize transfers through the bus abstraction.

A Linux I2C adapter number must not be assumed stable. Resolve and configure the correct adapter path before hardware access.

The provisional software default is that Board 1 and the Board 2 SHT45 share one named I2C adapter configuration. The code must still support an adapter override per board or device. This assumption does not authorize physical connection before electrical validation.

The boards are independently powered and contain their own approximately 10 kOhm I2C pull-ups. Before joining Board 1, Board 2, and Raspberry Pi I2C wiring, verify the total effective pull-up resistance, compatible 3.3 V logic levels, common ground, and absence of unsafe back-powering when one board is off.

---

## SPI Rules

- ADS114S06 uses standard SPI, not QSPI.
- Use Linux `spidev` for the initial user-space implementation.
- Keep the SPI device path configurable.
- Configure SPI mode, bit order, word size, and clock explicitly.
- Represent the confirmed ADS114S06 always-selected hardware behavior explicitly; do not add an ADC CS GPIO.
- Use GPIO edge handling for the confirmed `DRDY#` line after the Raspberry Pi GPIO mapping is selected.
- Use a configured GPIO output for the confirmed `START` line if the chosen conversion mode requires it.
- Do not expect a Raspberry Pi RESET line; ADS114S06 `RESET#` is tied to IOVDD on the board.
- Do not assume a display and ADS114S06 can share identical SPI settings.
- Do not hold chip select active longer than required by the protocol.

If a device-tree change is required to expose the ADS114S06 SPI connection through `spidev`, document the required overlay separately and do not silently edit boot configuration.

---

## Analog Conversion Rules

Keep these stages separate:

```text
ADC raw code
    -> ADC input voltage
    -> sensor electrical quantity
    -> calibrated or normalized feature
    -> estimated gas concentration, when valid
```

For TGS sensors, do not combine these into one opaque function.

For ADS114S06 voltage conversion, use the confirmed external 4.096 V reference on REFP0/REFN0, subject to the final selected PGA and bipolar/unipolar transfer equation from the datasheet.

For electrochemical sensors, keep these stages separate:

```text
ADC code
    -> analog-front-end voltage
    -> sensor current
    -> zero-corrected current
    -> gas concentration
```

Concentration values must not be reported as valid until the analog-front-end transfer function and calibration constants are known.

Until then, report raw ADC code and voltage only.

---

## Configuration Rules

Use an external runtime configuration file for machine-specific Linux device paths, provisional operating settings, and confirmed hardware mappings.

Use TOML for this repository unless the existing code has already adopted another format consistently.

The current-phase configuration must include fields equivalent to:

```text
named shared I2C adapter path, disabled or empty by default
optional per-board or per-device I2C adapter override
SPI device path, disabled or empty by default
GPIO chip label or path
GPIO line offsets and active levels for START and DRDY#
confirmed BME690 address 0x76
confirmed MCP3421 addresses 0x69 and 0x6A
ADS114S06 SPI settings and always-selected board behavior
sensor enable flags
sensor sampling intervals
SHT45 precision and heater policy
SGP41 cadence and SHT45-compensation freshness policy
BME690 measurement-profile parameters
MCP3421 resolution, PGA gain, and conversion mode
ADS114S06 PGA, data rate, filter, and conversion-sequencing settings
raw logging enable flag, schema version, and output path
```

Provisional defaults must be clearly labeled in the example configuration. Machine-specific device paths and GPIO mappings must remain unset or disabled until explicitly configured.

Do not place credentials or unrelated personal data in the configuration file.

Do not commit a machine-specific configuration containing local device paths or unsafe assumptions unless explicitly requested.

Validate configuration completely before opening devices or enabling hardware outputs. Unsupported combinations must produce a clear startup error rather than silently falling back to another mode.

---

## Error Handling

Define explicit error and validity states for conditions including:

- configuration missing
- permission denied
- Linux device path missing
- I2C communication failure
- SPI communication failure
- GPIO request failure
- CRC failure
- timeout
- ADC saturation
- sensor not detected
- invalid measurement
- stale measurement
- heater not stable
- conditioning incomplete
- missing calibration
- analog input outside expected range
- application shutdown requested

Do not hide errors by returning zero.

Do not use infinite retry loops.

Use bounded retries with backoff where retries are appropriate.

Log actionable context, including device type, bus path, address, and operation, without flooding the log on every loop iteration.

---

## Logging

Basic versioned raw-data logging is part of the current software phase. The initial implementation may use stdout for diagnostics and CSV files for measurement data.

Requirements:

- include an explicit schema version
- keep the CSV field order stable within one schema version
- include monotonic-derived timing information and wall-clock timestamps as appropriate
- include system state
- include raw readings and currently valid physical quantities
- include per-sensor validity and error information
- keep human-readable diagnostic logs separate from CSV measurement rows
- never substitute zero for unavailable or invalid data
- flush safely without forcing a disk sync on every sample
- allow logging to be disabled through configuration
- fail clearly when the configured output path cannot be opened

Production-grade log rotation, disk-space limits, crash recovery, abrupt-power-loss handling, and long-term storage management will be added later.

Use stdout for normal diagnostics and stderr for warnings and errors, or use one structured logging library consistently.

Do not write high-rate data indefinitely without disk-space management once long-running deployment begins.

---

## Display Scope

The display will be integrated later.

Do not assume that the ESP32 display wiring or LVGL hardware driver can be reused unchanged on Raspberry Pi 5.

Possible Raspberry Pi display paths include HDMI, DSI/DRM, framebuffer/DRM, or a separate SPI display, but the actual hardware interface must be confirmed.

LVGL may still be used at the application layer, but its Linux display and input backend must be selected separately.

Do not implement display code until the following are confirmed:

- display model
- display interface
- resolution
- touch-controller model
- touch interface
- Linux device/API used by the display and touch input

Keep sensor acquisition independent from display refresh.

Display failure must not stop sensor acquisition.

---

## Airflow-Control Scope

Pump and valve control will be integrated later.

Before enabling any actuator, confirm:

- Raspberry Pi GPIO line
- active-high or active-low behavior
- driver circuit
- default power-on state
- safe shutdown state
- whether PWM is required
- whether Linux PWM or simple GPIO control is appropriate

The application must place actuators in a safe state on normal shutdown and where practical on startup failures.

Do not drive an actuator directly from Raspberry Pi GPIO.

---

## Coding Style

- Use descriptive names.
- Use fixed-width integer types when data width matters.
- Prefer `constexpr` for compile-time constants.
- Use `enum class` instead of unscoped enums where practical.
- Use `nullptr` instead of `NULL`.
- Use RAII for file descriptors, GPIO line requests, threads, and other Linux resources.
- Close file descriptors cleanly.
- Keep functions focused and reasonably short.
- Avoid unnecessary dynamic allocation in recurring acquisition paths.
- Do not use exceptions across hardware-driver boundaries unless the project adopts one explicit exception policy.
- Avoid unnecessary global variables.
- Document units in variable names or comments.
- Use suffixes such as `Ms`, `V`, `A`, `Ohm`, `Pa`, `C`, `Rh`, and `Ppm`.
- Do not mix raw and converted values under ambiguous names such as `value`.
- Use `std::span`, `std::array`, and `std::chrono` where they improve clarity.
- Compile with warnings enabled.

Example:

```cpp
float voltageV;
float resistanceOhm;
std::chrono::milliseconds conversionTimeout;
```

---

## Dependency Rules

Prefer:

- Linux kernel userspace interfaces
- official manufacturer libraries or reference drivers
- `libgpiod` for GPIO
- small, actively maintained C++ dependencies

Before adding a dependency:

1. Check whether it is official or actively maintained.
2. Explain why it is needed.
3. Record it in CMake and README documentation.
4. Avoid multiple libraries that perform the same task.
5. Confirm Raspberry Pi OS package availability or document source-build requirements.

Do not add Arduino, PlatformIO, ESP-IDF, `Wire`, `SPIClass`, or ESP32-specific dependencies to the Raspberry Pi version.

Do not use wiringPi.

Do not silently upgrade or replace major dependencies.

---

## Build and Validation

Use CMake for the Raspberry Pi 5 version.

Default native build commands:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

After changing compilable application code:

1. Configure the build.
2. Build the project.
3. Run unit tests.
4. Report whether each step succeeded.
5. Fix the first meaningful build or test error when within scope.

Do not claim hardware verification from compilation or unit tests.

Clearly distinguish:

- build-verified
- unit-tested with mocks
- tested on Raspberry Pi without sensors
- hardware-verified with connected devices

Do not change Raspberry Pi boot configuration, device-tree overlays, user groups, udev rules, or system services without explicit permission.

Do not run hardware tests that can activate pumps, heaters, or valves without explicit permission.

---

## Testing Rules

Drivers must be testable with mocks.

Unit tests should cover where practical:

- command encoding
- byte order
- CRC validation
- signed ADC conversion
- raw-code-to-voltage calculations
- timeout behavior
- error propagation
- stale-data detection
- scheduling state transitions

Hardware-integration tests should be separate executables or explicitly selected test modes.

Do not run an I2C scan or write commands to hardware as part of ordinary unit tests.

Hardware tests must identify the target adapter and device explicitly.

---

## Process Lifecycle and Shutdown

The application should handle termination signals such as `SIGINT` and `SIGTERM`.

On shutdown:

- stop scheduling new measurements
- finish or cancel bounded in-progress operations
- place actuators in their configured safe state
- flush and close logs
- release GPIO lines
- close I2C and SPI file descriptors
- join worker threads

Do not perform complex non-async-signal-safe work directly inside a signal handler. Use the signal handler to set a shutdown flag or use a suitable signal-waiting design.

---

## Git and Change Management

- Make focused changes.
- Do not rewrite unrelated files.
- Do not delete working ESP32 code until the Raspberry Pi replacement exists and the requested migration scope permits deletion.
- In this duplicated Raspberry Pi repository, remove obsolete ESP32-specific files only after identifying their Raspberry Pi replacements.
- Summarize files created, changed, renamed, or removed.
- Highlight assumptions and unresolved hardware questions.
- Do not commit automatically unless explicitly requested.
- Do not include credentials, API keys, or private data.

---

## Current Development Order

The CMake/Linux scaffold and mock-host build already exist. Use this current-phase order unless the user explicitly changes it:

1. Re-read this `AGENTS.md` and inspect the current repository before editing.
2. Preserve safe startup behavior: hardware access remains disabled when device paths or GPIO mappings are missing.
3. Add the provisional shared-I2C configuration model and initial runtime operating profile.
4. Implement and test SHT45 protocol logic at `0x44`.
5. Implement and test SGP41 protocol logic at `0x59`, including SHT45 compensation and conditioning state.
6. Implement and test BME690 I2C protocol logic at `0x76` using a simple configurable forced-measurement profile.
7. Implement and test the generic MCP3421 driver, then instantiate NH3 at `0x69` and H2S at `0x6A`.
8. Implement and test ADS114S06 command/register logic, confirmed channel mapping, signed raw conversion, and external-reference voltage conversion.
9. Add bounded, non-blocking SensorManager scheduling for sensors with different cadences.
10. Implement the versioned raw `SensorFrame` and stable CSV schema.
11. Add or expand mock tests for normal reads, CRC failures, timeouts, signed values, stale compensation, missing configuration, and partial sensor failure.
12. Run the supported host CMake build and tests and report results.
13. Later, after the Raspberry Pi and assembled boards are available, finalize physical-header/BCM/GPIO mappings and Linux device paths.
14. Build natively on Raspberry Pi 5 and validate I2C, SPI, GPIO, electrical behavior, and real sensor data.
15. Add calibration storage and versioning only after raw acquisition is hardware-verified.
16. Add pump and valve control.
17. Add display integration.
18. Add systemd service deployment only after interactive execution is stable.
19. Add machine-learning inference only after reliable, calibrated data acquisition.

Do not block current driver and raw-logging implementation on unavailable Raspberry Pi wiring information. Keep those values configurable and clearly unverified.

---

## ESP32-to-Raspberry-Pi Migration Rules

During migration, replace these concepts:

```text
PlatformIO             -> CMake
Arduino framework      -> Linux/POSIX and C++ standard library
setup()/loop()          -> main() and explicit application loop
millis()                -> std::chrono::steady_clock
Wire / TwoWire          -> Linux I2C adapter abstraction
SPI / SPIClass          -> Linux spidev abstraction
pinMode/digitalWrite    -> libgpiod GPIO requests
Arduino Serial          -> stdout/stderr or explicit serial device
ESP32 NVS/Preferences   -> configuration/calibration files with safe persistence
FreeRTOS tasks          -> Linux threads only where justified
ESP32 Flash/PSRAM       -> Raspberry Pi RAM and filesystem storage
```

Do not mechanically rename ESP32 APIs while preserving incompatible assumptions.

Preserve platform-independent code where correct:

- sensor command definitions
- CRC algorithms
- measurement structures
- ADC conversion math
- calibration models
- SensorManager concepts
- logging schemas

Refactor platform-dependent code behind interfaces rather than duplicating Linux system calls in each sensor driver.

Remove ESP32-specific include files, build flags, and board configuration only when their Raspberry Pi replacements are in place.

---

## Response Expectations

Before making a large change:

- read this `AGENTS.md`
- inspect the existing repository
- identify ESP32-specific and platform-independent code
- state the migration plan briefly
- identify missing Raspberry Pi hardware mappings and Linux device paths
- preserve buildability whenever practical

After making changes, report:

1. What was changed
2. Which files were added, renamed, modified, or removed
3. Build and unit-test results
4. Which code was preserved from the ESP32 version
5. Assumptions made
6. Remaining hardware or operating-system information needed
7. Any commands the user must run manually on Raspberry Pi
8. The recommended next step

Do not claim that hardware behavior has been verified unless it was tested on the Raspberry Pi 5 with the actual connected sensor board.
