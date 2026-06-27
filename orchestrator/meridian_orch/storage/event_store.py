"""
In-memory event and effect store.
Production deployments would swap this for Redis / TimescaleDB.
"""
from __future__ import annotations

import time
from collections import defaultdict, deque
from dataclasses import dataclass, field
from threading import Lock
from typing import Deque

from ..api.models import EffectRecord, MetricPoint


@dataclass
class EventStore:
    """Thread-safe in-memory store for metrics and effects."""

    _metric_history: dict[str, Deque[MetricPoint]]   = field(default_factory=dict)
    _effects:        Deque[EffectRecord]               = field(default_factory=deque)
    _lock:           Lock                              = field(default_factory=Lock)
    max_points:      int                               = 1000
    max_effects:     int                               = 5000

    def __post_init__(self):
        self._metric_history  = defaultdict(lambda: deque(maxlen=self.max_points))
        self._effects         = deque(maxlen=self.max_effects)

    # ── Metrics ───────────────────────────────────────────────────────────────

    def record_metric(self, key: str, value: float, ts_ms: int | None = None) -> None:
        point = MetricPoint(
            timestamp_ms=ts_ms or int(time.time() * 1000),
            value=value,
        )
        with self._lock:
            self._metric_history[key].append(point)

    def get_metric_history(
        self, key: str, since_ms: int = 0, limit: int = 200
    ) -> list[MetricPoint]:
        with self._lock:
            points = list(self._metric_history.get(key, []))
        if since_ms:
            points = [p for p in points if p.timestamp_ms >= since_ms]
        return points[-limit:]

    def list_metrics(self) -> list[str]:
        with self._lock:
            return list(self._metric_history.keys())

    def latest_value(self, key: str) -> float | None:
        with self._lock:
            deq = self._metric_history.get(key)
            if not deq:
                return None
            return deq[-1].value

    # ── Effects ───────────────────────────────────────────────────────────────

    def record_effect(self, effect: EffectRecord) -> None:
        with self._lock:
            self._effects.append(effect)

    def get_effects(self, limit: int = 100) -> list[EffectRecord]:
        with self._lock:
            return list(self._effects)[-limit:]

    def effect_count(self) -> int:
        with self._lock:
            return len(self._effects)

    # ── Housekeeping ──────────────────────────────────────────────────────────

    def clear(self) -> None:
        with self._lock:
            self._metric_history.clear()
            self._effects.clear()
