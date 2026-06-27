"""
Synthetic metric generator for demos and load testing.

Generates realistic CPU, memory, latency, and error-rate time series
with configurable anomaly injection so the pipeline actually fires alerts.
"""
from __future__ import annotations

import asyncio
import math
import random
import time
from dataclasses import dataclass, field
from typing import AsyncIterator, Callable

from ..api.models import MetricIngest


@dataclass
class GeneratorConfig:
    source:          str   = "synthetic-host"
    interval_ms:     float = 500.0           # ms between ticks
    cpu_mean:        float = 40.0
    cpu_stddev:      float = 8.0
    mem_mean:        float = 60.0
    mem_stddev:      float = 5.0
    latency_mean_ms: float = 50.0
    latency_stddev:  float = 15.0
    anomaly_prob:    float = 0.04            # 4% chance of spike per tick
    anomaly_scale:   float = 3.5            # σ multiplier for anomaly injection


class SyntheticGenerator:
    """
    Streams synthetic MetricIngest events via an async generator.

    Usage::

        gen = SyntheticGenerator(config)
        async for metric in gen.stream():
            await publish(metric)
    """

    def __init__(self, config: GeneratorConfig | None = None) -> None:
        self.config  = config or GeneratorConfig()
        self._rng    = random.Random(int(time.time()))
        self._tick   = 0
        self._running = False

    def _sample(self, mean: float, stddev: float, anomaly: bool) -> float:
        base = self._rng.gauss(mean, stddev)
        if anomaly:
            base += self._rng.gauss(mean * self.config.anomaly_scale, stddev)
        return max(0.0, min(100.0, base))

    def _next_metrics(self) -> list[MetricIngest]:
        cfg     = self.config
        inject  = self._rng.random() < cfg.anomaly_prob
        t       = self._tick * cfg.interval_ms / 1000.0

        # Sinusoidal diurnal pattern on top of Gaussian noise
        diurnal = 10.0 * math.sin(2 * math.pi * t / 3600.0)

        cpu_val = max(0.0, min(100.0,
            cfg.cpu_mean + diurnal + self._rng.gauss(0, cfg.cpu_stddev)
            + (cfg.cpu_mean * cfg.anomaly_scale if inject else 0.0)
        ))
        mem_val = max(0.0, min(100.0,
            cfg.mem_mean + self._rng.gauss(0, cfg.mem_stddev)
            + (20.0 if inject else 0.0)
        ))
        lat_val = max(0.0,
            cfg.latency_mean_ms + self._rng.gauss(0, cfg.latency_stddev)
            + (cfg.latency_mean_ms * 5 if inject else 0.0)
        )
        err_val = max(0.0, self._rng.gauss(0.5, 0.3) + (15.0 if inject else 0.0))

        self._tick += 1
        return [
            MetricIngest(source=cfg.source, metric_name="cpu.usage",        value=cpu_val,  unit="percent"),
            MetricIngest(source=cfg.source, metric_name="memory.used_pct",   value=mem_val,  unit="percent"),
            MetricIngest(source=cfg.source, metric_name="http.latency_p99",  value=lat_val,  unit="ms"),
            MetricIngest(source=cfg.source, metric_name="http.error_rate",   value=err_val,  unit="percent"),
        ]

    async def stream(self) -> AsyncIterator[MetricIngest]:
        """Yield one MetricIngest every interval_ms milliseconds."""
        self._running = True
        try:
            while self._running:
                for m in self._next_metrics():
                    yield m
                await asyncio.sleep(self.config.interval_ms / 1000.0)
        finally:
            self._running = False

    def stop(self) -> None:
        self._running = False

    def burst(self, n: int) -> list[MetricIngest]:
        """Generate N metrics synchronously (useful for tests / replay)."""
        out: list[MetricIngest] = []
        for _ in range(n):
            out.extend(self._next_metrics())
        return out
