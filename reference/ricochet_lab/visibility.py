"""Toy server-side interest filtering, independent of any Call of Duty game.

Positions and boxes must come from the sandbox server's state. This geometry
demonstration does not implement a production map, network snapshot protocol,
visibility policy, or RICOCHET detection. Occluders are closed opaque boxes:
touching a box, including at a segment endpoint, counts as obstruction.
"""

from __future__ import annotations

import math
import re
from collections.abc import Iterable

MAX_ENTITIES = 4096
MAX_OCCLUDERS = 256
MAX_COORDINATE = 1_000_000_000.0
_IDENTIFIER = re.compile(r"[A-Za-z0-9_.:-]{1,128}\Z")


def _number(value: object) -> float:
    if type(value) not in (int, float):
        raise ValueError("coordinates and distance must be finite numbers")
    try:
        number = float(value)
    except (ValueError, OverflowError) as exc:
        raise ValueError("number exceeds the supported coordinate range") from exc
    if not math.isfinite(number) or abs(number) > MAX_COORDINATE:
        raise ValueError("number exceeds the supported coordinate range")
    return number


def _point(value: object) -> tuple[float, float, float]:
    if not isinstance(value, (list, tuple)) or len(value) != 3:
        raise ValueError("a position must have exactly three coordinates")
    return tuple(_number(coordinate) for coordinate in value)


def _identifier(value: object) -> str:
    if not isinstance(value, str) or _IDENTIFIER.fullmatch(value) is None:
        raise ValueError("entity IDs must be bounded ASCII identifiers")
    return value


def _blocked(start: tuple, end: tuple, lower: tuple, upper: tuple) -> bool:
    """Intersect a finite closed segment with a closed axis-aligned box."""
    enter, leave = 0.0, 1.0
    for axis in range(3):
        delta = end[axis] - start[axis]
        if delta == 0.0:
            if start[axis] < lower[axis] or start[axis] > upper[axis]:
                return False
            continue
        first = (lower[axis] - start[axis]) / delta
        second = (upper[axis] - start[axis]) / delta
        enter = max(enter, min(first, second))
        leave = min(leave, max(first, second))
        if enter > leave:
            return False
    return True


def visible_entities(
    observer_id: str,
    entities: Iterable[dict],
    *,
    max_distance: float,
    occluders: Iterable[dict] = (),
) -> list[dict]:
    """Return copied visible entities in input order, including the observer.

    Entities have exactly ``id`` and ``position`` fields. Boxes have exactly
    ``min`` and ``max`` fields. The observer must exist once; all IDs are unique.
    The distance boundary is inclusive. Invalid or excessive input raises
    ``ValueError`` before a result is returned. No client-provided override can
    disable the range or occlusion checks.
    """
    observer_id = _identifier(observer_id)
    distance = _number(max_distance)
    if distance < 0:
        raise ValueError("max_distance must be nonnegative")
    normalized = []
    positions = {}
    try:
        iterator = iter(entities)
    except TypeError as exc:
        raise ValueError("entities must be iterable") from exc
    for index, entity in enumerate(iterator):
        if index >= MAX_ENTITIES:
            raise ValueError("too many entities")
        if not isinstance(entity, dict) or set(entity) != {"id", "position"}:
            raise ValueError("entity must contain exactly id and position")
        identifier = _identifier(entity["id"])
        position = _point(entity["position"])
        if identifier in positions:
            raise ValueError("duplicate entity ID")
        normalized.append({"id": identifier, "position": list(position)})
        positions[identifier] = position
    if observer_id not in positions:
        raise ValueError("observer is missing")

    boxes = []
    try:
        iterator = iter(occluders)
    except TypeError as exc:
        raise ValueError("occluders must be iterable") from exc
    for index, box in enumerate(iterator):
        if index >= MAX_OCCLUDERS:
            raise ValueError("too many occluders")
        if not isinstance(box, dict) or set(box) != {"min", "max"}:
            raise ValueError("box must contain exactly min and max")
        lower, upper = _point(box["min"]), _point(box["max"])
        if any(lower[axis] > upper[axis] for axis in range(3)):
            raise ValueError("box min must not exceed max")
        boxes.append((lower, upper))

    start = positions[observer_id]
    visible = []
    for entity in normalized:
        if entity["id"] == observer_id:
            visible.append(entity)
            continue
        end = positions[entity["id"]]
        if math.dist(start, end) > distance:
            continue
        if any(_blocked(start, end, lower, upper) for lower, upper in boxes):
            continue
        visible.append(entity)
    return visible
