from __future__ import annotations

import copy
import math
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
        self.last_device_error: str | None = None
        self.downstream_healthy: bool | None = None
        self.downstream_error: str | None = None
        self.firmware_diagnostics_enabled: bool | None = None
        self.estop_active: bool | None = None
        self._downstream_protocol_error_latched = False

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
            with self._lock:
                self._apply_status(record)
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
                    self._downstream_protocol_error_latched = True
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
        if isinstance(downstream, dict) and not self._downstream_protocol_error_latched:
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
                estop = record.get("estop")
                if isinstance(estop, bool):
                    self.estop_active = estop
                self._downstream_protocol_error_latched = False
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
