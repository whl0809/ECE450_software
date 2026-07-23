# Bosch E-nose acquisition

Small, synchronous Python 3.11 application for acquiring the two E-nose
prototype boards at 1 Hz on a Raspberry Pi 5.

## Safety and wiring

Power each board from its own USB-C connector and connect a common ground.
Do **not** connect Raspberry Pi 3.3 V or 5 V to either H1 header. Wire the bus
by net name because the two H1 connectors swap SDA and SCL:

- Raspberry Pi GPIO2 / pin 3 -> both SDA nets
- Raspberry Pi GPIO3 / pin 5 -> both SCL nets
- Raspberry Pi ground -> both DGND nets

## Install and run

```bash
python -m venv .venv
source .venv/bin/activate
python -m pip install -e .
python -m enose probe --config config/rpi5.toml
python -m enose diagnose --config config/rpi5.toml
python -m enose acquire --config config/rpi5.toml
```

Use `--verbose` after the subcommand for byte-level diagnostics. A bounded
acquisition can be run with `--frames 60`.

The BME690 driver uses Bosch Sensortec's official BME690 SensorAPI v1.1.0
through a small native extension built during installation. It runs the sensor
in forced mode with the heater settings from `config/rpi5.toml`. BME690 remains
optional in the default configuration, so its initialization or read failures
do not stop the other sensors.

Sensirion's official `sensirion-gas-index-algorithm` package processes each
new 1 Hz SGP41 raw sample into VOC Index and NOx Index while preserving both
raw signals. These indices are not ppm or concentration measurements.
