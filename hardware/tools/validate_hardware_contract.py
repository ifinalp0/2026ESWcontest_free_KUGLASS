#!/usr/bin/env python3
"""Validate as-built hardware metadata against firmware pin contracts."""

from __future__ import annotations

import hashlib
import json
import math
import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
HARDWARE_DIR = REPO_ROOT / "hardware"


def fail(message: str) -> None:
    raise ValueError(message)


def load_json(relative_path: str) -> dict:
    path = REPO_ROOT / relative_path
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        fail(f"{relative_path} must contain a JSON object")
    if value.get("schema_version") != 1:
        fail(f"{relative_path} has an unsupported schema_version")
    return value


def validate_integrity() -> set[str]:
    sums_path = HARDWARE_DIR / "SHA256SUMS"
    seen: set[str] = set()
    for line_number, raw_line in enumerate(
        sums_path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        if not raw_line.strip():
            continue
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", raw_line)
        if match is None:
            fail(f"SHA256SUMS:{line_number} has invalid syntax")
        expected, relative_path = match.groups()
        if relative_path in seen:
            fail(f"SHA256SUMS lists {relative_path} more than once")
        seen.add(relative_path)
        path = REPO_ROOT / relative_path
        if not path.is_file():
            fail(f"missing hardware source: {relative_path}")
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != expected:
            fail(f"hash mismatch: {relative_path}")
    if not seen:
        fail("SHA256SUMS is empty")
    return seen


def validate_manifest(manifest: dict, integrity_paths: set[str]) -> None:
    if manifest.get("authority", {}).get("assembly_state") != "manufactured_as_built":
        fail("manifest must mark the hardware as manufactured_as_built")
    boards = manifest.get("boards")
    if not isinstance(boards, dict) or set(boards) != {"logic_carrier", "power_stage"}:
        fail("manifest must describe logic_carrier and power_stage")
    if boards["logic_carrier"].get("quantity") != 1:
        fail("Logic Carrier quantity must be 1")
    if boards["power_stage"].get("quantity") != 4:
        fail("Power Stage quantity must be 4")
    for board_name, board in boards.items():
        if board.get("manufactured") is not True:
            fail(f"{board_name} must be marked manufactured")
        for source_group in ("primary_sources", "component_sources"):
            for source in board.get(source_group, []):
                path = source.get("path")
                if not isinstance(path, str) or not (REPO_ROOT / path).is_file():
                    fail(f"{board_name} references missing source {path!r}")
                if path not in integrity_paths:
                    fail(
                        f"{board_name} source is not protected by SHA256SUMS: {path}"
                    )


def expected_channel_tuple(channel: dict) -> tuple[int, ...]:
    return (
        channel["pwm_mag_gpio"],
        channel["direction_gpio"],
        channel["mcu_enable_gpio"],
        channel["fault_n_gpio"],
        channel["current_adc_gpio"],
        channel["temperature_adc_gpio"],
    )


def validate_io_contract(io_contract: dict, power_contract: dict) -> None:
    if io_contract.get("status") != "as_built":
        fail("ESP32_B IO contract must be as_built")
    channels = io_contract.get("channels")
    if not isinstance(channels, list) or len(channels) != 4:
        fail("ESP32_B IO contract must contain four channels")
    if [channel.get("channel_id") for channel in channels] != [0, 1, 2, 3]:
        fail("channel IDs must be exactly 0, 1, 2, 3")

    all_gpio = [io_contract["common"]["estop_n_gpio"]]
    j10_pins = power_contract["j10"]["pins"]
    signal_keys = [
        "pwm_mag",
        "direction",
        "gated_enable",
        "fault_n",
        "current_adc_raw",
        "temperature_adc_raw",
        "3v3",
        "12v",
    ]
    expected_j10 = [1, 3, 5, 7, 9, 11, 13, 15]
    if sorted(int(pin) for pin in j10_pins) != expected_j10:
        fail("Power Stage J10 odd pin pattern is invalid")

    for channel in channels:
        all_gpio.extend(expected_channel_tuple(channel))
        channel_id = channel["channel_id"]
        expected_j7 = [pin + channel_id * 16 for pin in expected_j10]
        actual_j7 = [channel["j7"][key] for key in signal_keys]
        if actual_j7 != expected_j7:
            fail(f"CH{channel_id} J7 mapping does not match the J10 pattern")
    if len(all_gpio) != len(set(all_gpio)):
        fail("Logic Carrier GPIO ownership is not unique")


def validate_firmware_pinmap(io_contract: dict) -> None:
    pinmap_path = REPO_ROOT / "ESP32_B_Algo/main/power_stage_pinmap.h"
    pinmap_text = pinmap_path.read_text(encoding="utf-8")
    estop_match = re.search(
        r"KUGLASS_POWER_STAGE_PINMAP\s*=\s*\{\s*(\d+)\s*,",
        pinmap_text,
    )
    if estop_match is None:
        fail("could not parse ESP32_B estop pin")
    if int(estop_match.group(1)) != io_contract["common"]["estop_n_gpio"]:
        fail("ESP32_B estop pin differs from the hardware contract")

    firmware_channels = [
        tuple(int(value) for value in values)
        for values in re.findall(
            r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}",
            pinmap_text,
        )
    ]
    expected_channels = [
        expected_channel_tuple(channel) for channel in io_contract["channels"]
    ]
    if firmware_channels != expected_channels:
        fail("ESP32_B power_stage_pinmap.h differs from the hardware contract")

    analog_path = REPO_ROOT / "ESP32_B_Algo/main/analog_monitor.h"
    analog_text = analog_path.read_text(encoding="utf-8")
    descriptors = [
        (int(channel), kind, int(gpio), int(adc_channel))
        for channel, kind, gpio, adc_channel in re.findall(
            r"\{(\d+),\s*AnalogInputKind::(CURRENT|TEMPERATURE),\s*(\d+),\s*(\d+)\}",
            analog_text,
        )
    ]
    expected_descriptors: list[tuple[int, str, int, int]] = []
    for channel in io_contract["channels"]:
        expected_descriptors.append(
            (
                channel["channel_id"],
                "CURRENT",
                channel["current_adc_gpio"],
                channel["current_adc1_channel"],
            )
        )
        expected_descriptors.append(
            (
                channel["channel_id"],
                "TEMPERATURE",
                channel["temperature_adc_gpio"],
                channel["temperature_adc1_channel"],
            )
        )
    if descriptors != expected_descriptors:
        fail("ESP32_B analog_monitor.h differs from the hardware contract")


def validate_tabui_temperature_model(power_contract: dict) -> None:
    temperature = power_contract["temperature_feedback"]
    sensor = temperature["sensor"]
    expected_constants = {
        "TH1_SUPPLY_MV_NOMINAL": temperature["nominal_supply_v"] * 1000.0,
        "TH1_PULLUP_OHM_NOMINAL": temperature["pullup_resistance_ohm"],
        "TH1_R25_OHM_NOMINAL": sensor["nominal_resistance_ohm_at_25c"],
        "TH1_B25_85_K_NOMINAL": sensor["b25_85_k"],
    }
    state_path = REPO_ROOT / "TabUI/backend/state.py"
    state_text = state_path.read_text(encoding="utf-8")
    for name, expected in expected_constants.items():
        match = re.search(rf"^{name}\s*=\s*([0-9]+(?:\.[0-9]+)?)$", state_text, re.M)
        if match is None:
            fail(f"could not parse TabUI temperature constant {name}")
        if not math.isclose(float(match.group(1)), float(expected)):
            fail(f"TabUI {name} differs from the Power Stage temperature contract")


def main() -> int:
    manifest = load_json("hardware/manifest.json")
    io_contract = load_json("hardware/contracts/esp32_b_io.json")
    power_contract = load_json("hardware/contracts/power_stage.json")
    safety_contract = load_json("hardware/contracts/safety.json")
    load_json("hardware/validation/current-status.json")

    integrity_paths = validate_integrity()
    validate_manifest(manifest, integrity_paths)
    validate_io_contract(io_contract, power_contract)
    validate_firmware_pinmap(io_contract)
    validate_tabui_temperature_model(power_contract)

    invariants = safety_contract.get("invariants")
    if not isinstance(invariants, list) or not invariants:
        fail("safety contract has no invariants")
    identifiers = [item.get("id") for item in invariants]
    if len(identifiers) != len(set(identifiers)) or any(not item for item in identifiers):
        fail("safety invariant IDs must be non-empty and unique")

    print("hardware contract ok")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"hardware contract error: {error}", file=sys.stderr)
        raise SystemExit(1)
