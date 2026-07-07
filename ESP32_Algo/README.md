# ESP32_Algo

ESP32-S3 firmware for KUGLASS PDLC power control.

The firmware receives 20 Hz compact JSON Lines from Raspberry Pi and locally handles:

- command TTL and safe-off
- 4 local channels per controller
- MI ramping
- 60 Hz sign/magnitude SPWM duty generation over a 16 kHz carrier
- local fault latch
- status JSON Lines telemetry

Controller #A is CH0-CH3 by default. Build controller #B with `KUGLASS_FIRST_CHANNEL=4` and `KUGLASS_CONTROLLER_ID=B`.

## ESP-IDF build

```bash
cd ESP32_Algo
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

Controller B example:

```bash
idf.py -D KUGLASS_FIRST_CHANNEL=4 -D KUGLASS_CONTROLLER_ID=B build
```

## Host parser test

```bash
g++ -std=c++17 -Wall -Wextra -I main main/protocol.cpp host_tests/test_protocol_parser.cpp -o /tmp/kuglass_protocol_test
/tmp/kuglass_protocol_test
```
