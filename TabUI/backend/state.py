from __future__ import annotations

import copy
import math
import re
import threading
import time
from collections import deque
from typing import Any, Callable


CHANNEL_NAMES = [
    "CH0 전면 좌측",
    "CH1 전면 우측",
    "CH2 좌측 전방 도어",
    "CH3 우측 전방 도어",
]

SAFE_TOKEN = re.compile(r"^[A-Z0-9_]{1,48}$")
FAULT_CODES = {"NONE", "COMM_TIMEOUT", "INVALID_COMMAND", "POWER_STAGE_FAULT", "ESTOP"}
RESET_FAILURES = {"RESET_UNSAFE", "TARGET_BOOT_MISMATCH", "CHALLENGE_MISMATCH"}


def _default_adc_state() -> dict[str, Any]:
    return {
        "initialized": False,
        "currentCalibrated": False,
        "temperatureCalibrated": False,
        "rawValidMask": 0,
        "mvValidMask": 0,
        "channels": [
            {
                "channel": channel,
                "currentRaw": None,
                "temperatureRaw": None,
                "currentMv": None,
                "temperatureMv": None,
            }
            for channel in range(4)
        ],
    }


def _default_downstream_diagnostics() -> dict[str, Any]:
    return {
        "bootId": None,
        "statusSeq": None,
        "resetChallenge": None,
        "estopActive": None,
        "faultCode": None,
        "diagnostic": None,
        "operationalFault": False,
        "controlResult": None,
        "adc": _default_adc_state(),
    }


def clamp(value: Any, lower: float = 0.0, upper: float = 1.0) -> float:
    try:
        number = float(value)
    except (TypeError, ValueError):
        number = lower
    return max(lower, min(upper, number))


def optical_state(mi: float) -> str:
    if mi >= 0.72:
        return "CLEAR"
    if mi >= 0.30:
        return "DIM"
    return "FROST"


def estimated_transmittance(mi: float) -> float:
    return round(0.12 + 0.83 * (clamp(mi) ** 1.18), 3)


def default_state() -> dict[str, Any]:
    return {
        "schemaVersion": 1,
        "vehicleMode": "driving",
        "demoMode": "none",
        "environment": {
            "internalTemp": None,
            "frontLeftSaturation": 0.0,
            "frontRightSaturation": 0.0,
            "edgeDensity": 0.0,
        },
        "channels": [
            {
                "channel": channel,
                "name": name,
                "targetMi": 0.0,
                "commandedMi": 0.0,
                "appliedMi": 0.0,
                "appliedKnown": False,
                "estimatedTransmittance": 0.12,
                "opticalState": "FROST",
                "masterFault": False,
                "downstreamFault": False,
                "fault": False,
                "manualUntil": None,
            }
            for channel, name in enumerate(CHANNEL_NAMES)
        ],
        "cameraMetrics": {
            "valid": None,
            "aeMetadataValid": None,
            "frontLeftSaturation": 0.0,
            "frontRightSaturation": 0.0,
            "edgeDensity": 0.0,
            "glare": 0.0,
            "frameId": 0,
            "timestamp": 0.0,
        },
        "downstreamDiagnostics": _default_downstream_diagnostics(),
        "decisionReason": "ESP32_A 상태 텔레메트리를 기다리는 중입니다.",
        "timestamp": 0.0,
    }


ENV_ALIASES = {
    "internalTemp": ("internalTemp", "internal_temp_c", "temperature_c"),
    "frontLeftSaturation": ("frontLeftSaturation", "front_left_saturation", "left_saturation"),
    "frontRightSaturation": ("frontRightSaturation", "front_right_saturation", "right_saturation"),
    "edgeDensity": ("edgeDensity", "edge_density"),
}

CAMERA_ALIASES = {
    "frontLeftSaturation": ("frontLeftSaturation", "front_left_saturation", "left_saturation"),
    "frontRightSaturation": ("frontRightSaturation", "front_right_saturation", "right_saturation"),
    "edgeDensity": ("edgeDensity", "edge_density"),
    "glare": ("glare", "glare_score"),
    "frameId": ("frameId", "frame_id"),
    "timestamp": ("timestamp", "timestamp_s"),
}


def _first(mapping: dict[str, Any], aliases: tuple[str, ...]) -> Any:
    for key in aliases:
        if key in mapping:
            return mapping[key]
    return None


class StateStore:
    def __init__(self, time_fn: Callable[[], float] | None = None) -> None:
        self._lock = threading.RLock()
        self._state = default_state()
        self._replay: deque[dict[str, Any]] = deque(maxlen=600)
        self._time_fn = time_fn or time.time
        self.last_ack_seq: int | None = None
        self.last_ack_command: str | None = None
        self.last_ack_ok: bool | None = None
        self.last_device_error: str | None = None
        self.downstream_healthy: bool | None = None
        self.downstream_error: str | None = None
        self.firmware_diagnostics_enabled: bool | None = None
        self.estop_active: bool | None = None

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            return copy.deepcopy(self._state)

    def replay(self) -> list[dict[str, Any]]:
        with self._lock:
            return copy.deepcopy(list(self._replay))

    def apply_record(self, record: dict[str, Any]) -> bool:
        record_type = str(record.get("type", ""))
        if record_type == "ack":
            with self._lock:
                if record.get("seq") is not None:
                    self.last_ack_seq = int(record["seq"])
                command = record.get("command")
                self.last_ack_command = command if isinstance(command, str) else None
                self.last_ack_ok = record.get("ok") if isinstance(record.get("ok"), bool) else None
                if record.get("ok", True) is False:
                    self.last_device_error = str(record.get("error", "ESP32_A rejected command"))
                else:
                    self.last_device_error = None
            return True

        if record_type == "boot":
            controller_id = str(record.get("controller_id", record.get("controllerId", ""))).upper()
            if controller_id != "A":
                return False
            diagnostics = record.get("diagnostics_enabled", record.get("diagnosticsEnabled"))
            with self._lock:
                self.firmware_diagnostics_enabled = diagnostics if isinstance(diagnostics, bool) else None
            return True

        if record_type == "state":
            body = record.get("state") or record.get("payload") or record
            if not isinstance(body, dict):
                return False
            with self._lock:
                self._apply_state(body)
                self._record_snapshot()
            return True

        if record_type in {"status", "telemetry"}:
            controller_id = str(record.get("controller_id", record.get("controllerId", ""))).upper()
            normalized = None
            if controller_id == "B":
                normalized = _normalize_downstream_status(record)
                if normalized is None:
                    return False
            with self._lock:
                self._apply_status(normalized if normalized is not None else record)
                self._record_snapshot()
            return True

        if record_type in {"sensor", "sensors", "camera"}:
            with self._lock:
                self._apply_sensor_record(record)
                self._record_snapshot()
            return True

        if record_type == "protocol_error":
            with self._lock:
                source = str(record.get("source", "")).strip().lower().replace("-", "_")
                error = str(record.get("error", "ESP32_A protocol error"))
                if source in {"b", "esp32_b"}:
                    self.downstream_healthy = False
                    self.downstream_error = error
                else:
                    self.last_device_error = error
            return True
        return False

    def reset_firmware_handshake(self) -> None:
        with self._lock:
            self.firmware_diagnostics_enabled = None

    def _apply_state(self, body: dict[str, Any]) -> None:
        self._state["schemaVersion"] = int(body.get("schemaVersion", body.get("schema_version", 1)))
        vehicle_mode = body.get("vehicleMode", body.get("vehicle_mode", body.get("mode")))
        if vehicle_mode in {"driving", "stopped", "camping", "parked"}:
            self._state["vehicleMode"] = vehicle_mode
        demo_mode = body.get("demoMode", body.get("demo_mode"))
        if demo_mode in {"none", "hot_summer", "camping", "parked", "camera_saturation"}:
            self._state["demoMode"] = demo_mode

        environment = body.get("environment", body.get("sensors", {}))
        if isinstance(environment, dict):
            self._merge_environment(environment)
        camera = body.get("cameraMetrics", body.get("camera_metrics", body.get("camera", {})))
        if isinstance(camera, dict):
            self._merge_camera(camera)
        channels = body.get("channels", body.get("ch", []))
        if isinstance(channels, list):
            self._merge_channels(channels, source="master")
        downstream = body.get("downstream")
        if isinstance(downstream, dict):
            healthy = downstream.get("healthy")
            self.downstream_healthy = healthy if isinstance(healthy, bool) else None
            error = downstream.get("error")
            self.downstream_error = None if error is None else str(error)

        reason = body.get("decisionReason", body.get("decision_reason", body.get("reason")))
        if isinstance(reason, str) and reason:
            self._state["decisionReason"] = reason
        timestamp = body.get("timestamp", body.get("timestamp_s"))
        if timestamp is None and body.get("timestamp_ms") is not None:
            timestamp = float(body["timestamp_ms"]) / 1000.0
        self._state["timestamp"] = float(timestamp if timestamp is not None else self._time_fn())

    def _apply_status(self, record: dict[str, Any]) -> None:
        controller_id = str(record.get("controller_id", record.get("controllerId", ""))).upper()
        is_downstream = controller_id == "B"
        channels = record.get("channels", record.get("ch", []))
        if isinstance(channels, list):
            self._merge_channels(channels, source="downstream" if is_downstream else "master")
            if is_downstream:
                diagnostics = self._state["downstreamDiagnostics"]
                boot_changed = diagnostics["bootId"] not in {None, record["bootId"]}
                diagnostics.update({
                    "bootId": record["bootId"],
                    "statusSeq": record["seq"],
                    "resetChallenge": record["resetChallenge"],
                    "estopActive": record["estop"],
                    "faultCode": record["faultCode"],
                    "diagnostic": record.get("diagnostic"),
                    "operationalFault": record["operationalFault"],
                    "adc": record["adc"],
                })
                if boot_changed and record.get("controlResult") is None:
                    diagnostics["controlResult"] = None
                if record.get("controlResult") is not None:
                    diagnostics["controlResult"] = record["controlResult"]
                self.estop_active = record["estop"]
                self.downstream_healthy = True
                self.downstream_error = "NONE"
        if record.get("mode") in {"driving", "stopped", "camping", "parked"}:
            self._state["vehicleMode"] = record["mode"]
        if record.get("demo_mode") is not None:
            self._state["demoMode"] = record["demo_mode"]
        if record.get("reason"):
            self._state["decisionReason"] = str(record["reason"])
        self._state["timestamp"] = self._time_fn()

    def _apply_sensor_record(self, record: dict[str, Any]) -> None:
        environment = record.get("environment", record.get("sensors", record))
        if isinstance(environment, dict):
            self._merge_environment(environment)
        camera = record.get("cameraMetrics", record.get("camera_metrics", record.get("camera", record)))
        if isinstance(camera, dict):
            self._merge_camera(camera)
        self._state["timestamp"] = self._time_fn()

    def _merge_environment(self, incoming: dict[str, Any]) -> None:
        environment = self._state["environment"]
        for target, aliases in ENV_ALIASES.items():
            value = _first(incoming, aliases)
            if value is not None or any(alias in incoming for alias in aliases):
                environment[target] = None if value is None else float(value)

    def _merge_camera(self, incoming: dict[str, Any]) -> None:
        camera = self._state["cameraMetrics"]
        valid = incoming.get("valid", incoming.get("camera_valid"))
        if isinstance(valid, bool):
            camera["valid"] = valid
        ae_metadata_valid = incoming.get("aeMetadataValid", incoming.get("ae_metadata_valid"))
        if isinstance(ae_metadata_valid, bool):
            camera["aeMetadataValid"] = ae_metadata_valid
        for target, aliases in CAMERA_ALIASES.items():
            value = _first(incoming, aliases)
            if value is not None:
                camera[target] = int(value) if target == "frameId" else float(value)
        if "timestamp_ms" in incoming:
            camera["timestamp"] = float(incoming["timestamp_ms"]) / 1000.0

    def _merge_channels(self, incoming: list[Any], *, source: str) -> None:
        received_at = self._time_fn()
        for item in incoming:
            if not isinstance(item, dict):
                continue
            channel_value = item.get("channel", item.get("channel_id", item.get("id")))
            try:
                channel_id = int(channel_value)
            except (TypeError, ValueError):
                continue
            if channel_id < 0 or channel_id >= len(self._state["channels"]):
                continue
            current = self._state["channels"][channel_id]
            target_value = item.get("targetMi", item.get("target_mi"))
            commanded_value = item.get("commandedMi", item.get("commanded_mi"))
            if source == "master":
                if target_value is not None:
                    current["targetMi"] = round(clamp(target_value), 4)
                elif commanded_value is not None:
                    current["targetMi"] = round(clamp(commanded_value), 4)
                if commanded_value is not None:
                    current["commandedMi"] = round(clamp(commanded_value), 4)
                elif target_value is not None:
                    current["commandedMi"] = round(clamp(target_value), 4)
            applied_mi = current["appliedMi"]
            applied_known = current["appliedKnown"]
            if source == "downstream":
                applied_value = item.get("appliedMi", item.get("applied_mi", item.get("mi")))
                if applied_value is not None:
                    applied_mi = round(clamp(applied_value), 4)
                    applied_known = True
            master_fault = current["masterFault"]
            downstream_fault = current["downstreamFault"]
            if source == "master":
                master_value = item.get("masterFault", item.get("master_fault"))
                if master_value is None and "fault" in item:
                    master_value = item["fault"]
                if master_value is not None:
                    master_fault = bool(master_value)
            else:
                downstream_value = item.get("downstreamFault", item.get("downstream_fault"))
                if downstream_value is None and "fault" in item:
                    downstream_value = item["fault"]
                if downstream_value is not None:
                    downstream_fault = bool(downstream_value)
            current.update({
                "appliedMi": applied_mi,
                "appliedKnown": applied_known,
                "estimatedTransmittance": estimated_transmittance(applied_mi),
                "opticalState": optical_state(applied_mi),
                "masterFault": master_fault,
                "downstreamFault": downstream_fault,
                "fault": master_fault or downstream_fault,
            })
            if "manualUntil" in item:
                current["manualUntil"] = item["manualUntil"]
            elif "manual_until" in item:
                current["manualUntil"] = item["manual_until"]
            elif "manualRemainingMs" in item or "manual_remaining_ms" in item:
                remaining_value = item.get("manualRemainingMs", item.get("manual_remaining_ms"))
                try:
                    remaining_ms = float(remaining_value)
                except (TypeError, ValueError):
                    continue
                if not math.isfinite(remaining_ms) or remaining_ms <= 0.0:
                    current["manualUntil"] = None
                else:
                    current["manualUntil"] = received_at + remaining_ms / 1000.0

    def _record_snapshot(self) -> None:
        self._replay.append(copy.deepcopy(self._state))


def _session_identifier(value: Any) -> int | None:
    identifier = _non_negative_int(value)
    return identifier if identifier is not None and identifier > 0 else None


def _non_negative_int(value: Any, maximum: int = 0xFFFFFFFF) -> int | None:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0 or value > maximum:
        return None
    return value


def _safe_token(value: Any) -> str | None:
    return value if isinstance(value, str) and SAFE_TOKEN.fullmatch(value) else None


def _normalize_adc(adc: Any) -> dict[str, Any] | None:
    if not isinstance(adc, dict):
        return None
    bool_fields = ("initialized", "i_cali", "t_cali")
    if any(not isinstance(adc.get(field), bool) for field in bool_fields):
        return None
    raw_mask = _non_negative_int(adc.get("raw_valid_mask"), 0xFF)
    mv_mask = _non_negative_int(adc.get("mv_valid_mask"), 0xFF)
    arrays: dict[str, list[int]] = {}
    for field in ("i_raw", "t_raw", "i_mv", "t_mv"):
        value = adc.get(field)
        maximum = 4095 if field.endswith("raw") else 5000
        if (
            not isinstance(value, list)
            or len(value) != 4
            or any(
                isinstance(item, bool)
                or not isinstance(item, int)
                or item < 0
                or item > maximum
                for item in value
            )
        ):
            return None
        arrays[field] = value
    if raw_mask is None or mv_mask is None:
        return None

    channels = []
    for channel in range(4):
        current_bit = 1 << channel
        temperature_bit = 1 << (channel + 4)
        channels.append({
            "channel": channel,
            "currentRaw": arrays["i_raw"][channel] if raw_mask & current_bit else None,
            "temperatureRaw": arrays["t_raw"][channel] if raw_mask & temperature_bit else None,
            "currentMv": arrays["i_mv"][channel] if mv_mask & current_bit else None,
            "temperatureMv": arrays["t_mv"][channel] if mv_mask & temperature_bit else None,
        })
    return {
        "initialized": adc["initialized"],
        "currentCalibrated": adc["i_cali"],
        "temperatureCalibrated": adc["t_cali"],
        "rawValidMask": raw_mask,
        "mvValidMask": mv_mask,
        "channels": channels,
    }


def _normalize_control_result(value: Any) -> dict[str, Any] | None:
    if not isinstance(value, dict) or set(value) != {
        "command", "seq", "source_session_id", "ok", "error"
    }:
        return None
    if value.get("command") != "reset_fault" or not isinstance(value.get("ok"), bool):
        return None
    sequence = _non_negative_int(value.get("seq"))
    source_session_id = _session_identifier(value.get("source_session_id"))
    error = value.get("error")
    if sequence is None or source_session_id is None:
        return None
    if not isinstance(error, str):
        return None
    if value["ok"] and error != "NONE":
        return None
    if not value["ok"] and error not in RESET_FAILURES:
        return None
    return {
        "command": "reset_fault",
        "seq": sequence,
        "sourceSessionId": source_session_id,
        "ok": value["ok"],
        "error": error,
    }


def _normalize_downstream_status(record: dict[str, Any]) -> dict[str, Any] | None:
    if record.get("v") != 1 or record.get("type") != "status":
        return None
    if str(record.get("controller_id", "")).upper() != "B":
        return None
    sequence = _non_negative_int(record.get("seq"))
    boot_id = _session_identifier(record.get("boot_id"))
    reset_challenge = _session_identifier(record.get("reset_challenge"))
    estop = record.get("estop")
    fault_code = _safe_token(record.get("fault_code"))
    diagnostic_value = record.get("diagnostic")
    diagnostic = None if diagnostic_value is None else _safe_token(diagnostic_value)
    adc = _normalize_adc(record.get("adc"))
    if (
        sequence is None
        or boot_id is None
        or reset_challenge is None
        or not isinstance(estop, bool)
        or fault_code not in FAULT_CODES
        or diagnostic_value is not None and diagnostic is None
        or adc is None
    ):
        return None

    incoming_channels = record.get("ch")
    if not isinstance(incoming_channels, list) or len(incoming_channels) != 4:
        return None
    channels: list[dict[str, Any] | None] = [None] * 4
    for item in incoming_channels:
        if not isinstance(item, dict):
            return None
        channel = _non_negative_int(item.get("id"), 3)
        mi = item.get("mi")
        fault = item.get("fault")
        if (
            channel is None
            or channels[channel] is not None
            or isinstance(mi, bool)
            or not isinstance(mi, (int, float))
            or not math.isfinite(float(mi))
            or not 0.0 <= float(mi) <= 1.0
            or not isinstance(fault, bool)
        ):
            return None
        channels[channel] = {"id": channel, "mi": float(mi), "fault": fault}
    if any(channel is None for channel in channels):
        return None

    control_result = None
    if "control_result" in record:
        control_result = _normalize_control_result(record["control_result"])
        if control_result is None:
            return None
    normalized_channels = [channel for channel in channels if channel is not None]
    return {
        "type": "status",
        "controller_id": "B",
        "seq": sequence,
        "bootId": boot_id,
        "resetChallenge": reset_challenge,
        "estop": estop,
        "faultCode": fault_code,
        "diagnostic": diagnostic,
        "ch": normalized_channels,
        "adc": adc,
        "controlResult": control_result,
        "operationalFault": estop or fault_code != "NONE" or any(
            bool(channel["fault"]) for channel in normalized_channels
        ),
    }
