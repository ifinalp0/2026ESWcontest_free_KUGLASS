# RasPi_Algo

Raspberry Pi 5 software for KUGLASS.

This package follows the project plan and `Smart_glass_V20_0.md`:

- `kuglass-control.service`: perception, sensor fusion, policy, MI servo, ESP32 serial, CSV logging.
- `kuglass-ui.service`: UI/API process that reads `StateStore` and writes `CommandQueue`.
- Default execution is mock-safe. Hardware serial, cameras, I2C sensors, and Flask are optional runtime dependencies.

## Quick start on a dev machine

```bash
cd RasPi_Algo
python3 -m unittest discover -s tests
python3 -m kuglass.control_service --mock --iterations 5
python3 -m kuglass.ui_service --host 127.0.0.1 --port 8080
```

## Raspberry Pi deployment outline

```bash
cd RasPi_Algo
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
sudo cp systemd/kuglass-control.service /etc/systemd/system/
sudo cp systemd/kuglass-ui.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now kuglass-control kuglass-ui
```

Edit `config/config.yaml` and `config/carrier_pinmap.yaml` before connecting real hardware.

