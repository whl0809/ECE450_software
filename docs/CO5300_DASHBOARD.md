# CO5300 Odor-Sensing Dashboard

Copy the files into the existing repository:

```text
ECE450_software/
├── tools/
│   ├── co5300_qspi_test.py       # existing verified QSPI driver
│   ├── co5300_dashboard.py       # new dashboard
│   └── run_co5300_dashboard.sh   # new launcher
└── config/
    ├── co5300_init.json           # existing initialization table
    └── display_state.example.json # new example display values
```

Install Pillow:

```bash
sudo apt update
sudo apt install -y python3-pil python3-lgpio gpiod
```

Preview without hardware:

```bash
cd /home/pi/Documents/ECE450_software
python3 tools/co5300_dashboard.py \
  --state-file config/display_state.example.json \
  --preview dashboard_preview.png
```

Display the example values:

```bash
chmod +x tools/run_co5300_dashboard.sh
./tools/run_co5300_dashboard.sh
```

Generate changing demo values:

```bash
sudo python3 tools/co5300_dashboard.py \
  --demo \
  --refresh-seconds 3
```

Watch a live state file:

```bash
mkdir -p runtime
cp config/display_state.example.json runtime/display_state.json

sudo python3 tools/co5300_dashboard.py \
  --state-file runtime/display_state.json \
  --refresh-seconds 3
```

Another process can update `runtime/display_state.json`. Write it atomically:

```python
import json
from pathlib import Path

state = {
    "food_type": "Chicken",
    "freshness_level": "Fresh",
    "confidence": 0.92,
    "temperature_c": 24.3,
    "humidity_rh": 51.2,
    "voc_raw": 12345,
    "nox_raw": 2450,
    "nh3_value": 12.4,
    "nh3_unit": "mV",
    "h2s_value": 3.2,
    "h2s_unit": "mV",
    "system_status": "OK",
}

target = Path("runtime/display_state.json")
temporary = target.with_suffix(".tmp")
temporary.write_text(json.dumps(state), encoding="utf-8")
temporary.replace(target)
```

Until calibration is complete, keep VOC/NOx as SRAW and NH3/H2S as differential voltage rather than ppm. Food type, freshness, and confidence must come from the model/inference layer.
