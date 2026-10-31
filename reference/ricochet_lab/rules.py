"""Deterministic rules for an invented, server-authoritative sandbox.

These rules do not describe Call of Duty's gameplay, thresholds, entitlement
format, or detection algorithms. They evaluate supplied synthetic events; a
client report alone cannot establish trustworthy positions, times, or actions.
Production authority would supply observations from the actual game server.
No finding triggers a ban or any external action.
"""

from __future__ import annotations

import math
import re
import threading
from typing import Any, Iterable

MAX_EVENTS = 256
_ITEM = re.compile(r"[A-Za-z0-9._:-]{1,128}\Z")


def _number(value: Any, *, maximum: float = 1e12) -> bool:
    return (
        type(value) in (int, float)
        and 0 <= value <= maximum
        and math.isfinite(value)
    )


def _tick_time(payload: dict[str, Any]) -> bool:
    return (
        type(payload.get("tick")) is int
        and 0 <= payload["tick"] <= 2**53 - 1
        and _number(payload.get("time"))
    )


class RuleEngine:
    """One sandbox session's authority, configured by the verifier operator.

    Accepted movement and shots advance independent stream state. Rejected
    actions leave that state unchanged. There is intentionally no client reload,
    ammo reset, entitlement grant, or policy-change event. The first movement is
    only a baseline; this demonstration has no authoritative spawn-position map.
    All timestamps are in seconds and positions are arbitrary sandbox units.
    """

    def __init__(
        self, *, entitlements: Iterable[str] = (), max_speed: float = 8.0,
        initial_ammo: int = 6, min_shot_interval: float = 0.1,
    ) -> None:
        if not _number(max_speed, maximum=1e6) or max_speed <= 0:
            raise ValueError("max_speed must be finite and positive")
        if type(initial_ammo) is not int or not 0 <= initial_ammo <= 1_000_000:
            raise ValueError("initial_ammo must be a bounded nonnegative integer")
        if not _number(min_shot_interval, maximum=3600):
            raise ValueError("min_shot_interval must be finite and nonnegative")
        if isinstance(entitlements, (str, bytes)):
            raise ValueError("entitlements must be an iterable of item identifiers")
        items: set[str] = set()
        for item in entitlements:
            if not isinstance(item, str) or not _ITEM.fullmatch(item):
                raise ValueError("invalid entitlement item identifier")
            items.add(item)
            if len(items) > 1024:
                raise ValueError("too many entitlement items")
        self.entitlements = frozenset(items)
        self.max_speed = float(max_speed)
        self.min_shot_interval = float(min_shot_interval)
        self._ammo = initial_ammo
        self._movement: tuple[int, float, tuple[float, ...]] | None = None
        self._shot: tuple[int, float] | None = None
        self._lock = threading.Lock()

    @property
    def remaining_ammo(self) -> int:
        with self._lock:
            return self._ammo

    def analyze(self, events: list[dict[str, Any]]) -> list[dict[str, Any]]:
        """Evaluate at most MAX_EVENTS in order and preserve per-session state."""
        if not isinstance(events, list) or len(events) > MAX_EVENTS:
            raise ValueError("events must be a bounded list")
        findings = []
        with self._lock:
            for index, event in enumerate(events):
                finding = self._evaluate(index, event)
                if finding is not None:
                    findings.append(finding)
        return findings

    @staticmethod
    def _finding(index: int, rule: str, severity: str = "warning", **details: Any) -> dict[str, Any]:
        return {"rule_id": rule, "severity": severity, "evidence": {"event_index": index, **details}}

    def _evaluate(self, index: int, event: Any) -> dict[str, Any] | None:
        finding = self._finding
        if not isinstance(event, dict) or set(event) != {"kind", "payload"}:
            return finding(index, "LAB_EVENT_INVALID", reason="event must contain kind and payload")
        kind, payload = event["kind"], event["payload"]
        if not isinstance(kind, str) or not 1 <= len(kind) <= 64 or not isinstance(payload, dict):
            return finding(index, "LAB_EVENT_INVALID", reason="invalid kind or payload")
        if kind == "movement":
            if set(payload) != {"tick", "time", "position"} or not _tick_time(payload):
                return finding(index, "LAB_EVENT_INVALID", kind=kind, reason="expected tick, time and position")
            position = payload["position"]
            if not isinstance(position, list) or len(position) != 3 or any(
                type(value) not in (int, float) or abs(value) > 1e9 or not math.isfinite(value)
                for value in position
            ):
                return finding(index, "LAB_EVENT_INVALID", kind=kind, reason="position must contain three finite bounded numbers")
            tick, time = payload["tick"], float(payload["time"])
            point = tuple(float(value) for value in position)
            if self._movement is not None:
                old_tick, old_time, old_position = self._movement
                if tick <= old_tick or time <= old_time:
                    return finding(index, "LAB_MOVEMENT_ORDER", kind=kind, reason="tick and time must increase")
                distance = math.dist(point, old_position)
                allowed = self.max_speed * (time - old_time)
                if distance > allowed + 1e-9:
                    return finding(index, "LAB_MOVEMENT_SPEED", distance=distance, allowed_distance=allowed)
            self._movement = (tick, time, point)
            return None
        if kind == "shot":
            if set(payload) != {"tick", "time"} or not _tick_time(payload):
                return finding(index, "LAB_EVENT_INVALID", kind=kind, reason="expected tick and time")
            tick, time = payload["tick"], float(payload["time"])
            if self._shot is not None:
                old_tick, old_time = self._shot
                if tick <= old_tick or time <= old_time:
                    return finding(index, "LAB_SHOT_ORDER", reason="tick and time must increase")
                elapsed = time - old_time
                if elapsed + 1e-9 < self.min_shot_interval:
                    return finding(index, "LAB_SHOT_RATE", elapsed=elapsed, minimum_interval=self.min_shot_interval)
            if self._ammo == 0:
                return finding(index, "LAB_AMMO_EMPTY", remaining_ammo=0)
            self._ammo -= 1
            self._shot = (tick, time)
            return None
        if kind == "equip":
            if set(payload) != {"item"} or not isinstance(payload["item"], str) or not _ITEM.fullmatch(payload["item"]):
                return finding(index, "LAB_EVENT_INVALID", kind=kind, reason="expected one bounded item identifier")
            if payload["item"] not in self.entitlements:
                return finding(index, "LAB_ENTITLEMENT_MISSING", item=payload["item"])
            return None
        if kind == "kernel_observation":
            return None
        # Integrity reports and all other kinds cannot be
        # converted into a gameplay accusation by this small rule set.
        return finding(index, "LAB_EVENT_UNSUPPORTED", "info", kind=kind, reason="observation has no sandbox rule")


def analyze_events(events: list[dict[str, Any]], **server_config: Any) -> list[dict[str, Any]]:
    """Convenience for one batch; use RuleEngine for state across batches."""
    return RuleEngine(**server_config).analyze(events)
