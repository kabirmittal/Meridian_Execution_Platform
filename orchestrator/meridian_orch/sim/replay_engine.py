"""
Deterministic replay engine.

Takes a recorded scenario (list of metric events) and replays them
through an in-process pipeline registry, capturing all effects.
Enables "if we had deployed this processor yesterday, what would have fired?"
"""
from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Any

from ..api.models import EffectRecord, EffectType, ReplayResult


@dataclass
class ReplayEngine:
    """
    Replays scenario events and collects effects.
    Deliberately kept stateless between replays for determinism.
    """

    def replay(
        self,
        scenario_name: str,
        events: list[dict[str, Any]],
        pipeline_registry: dict[str, Any],
    ) -> ReplayResult:
        """
        Run all events through all registered pipelines and collect effects.

        In a real deployment this would marshal events to the C++ engine via
        a gRPC stub; for the demo it runs pure-Python simulation logic.
        """
        t0 = time.monotonic()
        captured_effects: list[EffectRecord] = []

        for ev in events:
            metric_name = ev.get("metric_name", "")
            value       = float(ev.get("value", 0))
            source      = ev.get("source", "replay")

            for pid, pipeline in pipeline_registry.items():
                effects = self._evaluate(metric_name, value, source, pid, pipeline)
                captured_effects.extend(effects)

        elapsed_ms = (time.monotonic() - t0) * 1000.0

        return ReplayResult(
            scenario_name    = scenario_name,
            events_replayed  = len(events),
            effects_captured = len(captured_effects),
            duration_ms      = round(elapsed_ms, 2),
            effects          = captured_effects,
        )

    # ── Internal evaluator ────────────────────────────────────────────────────

    def _evaluate(
        self,
        metric_name: str,
        value:       float,
        source:      str,
        pipeline_id: str,
        pipeline:    dict[str, Any],
    ) -> list[EffectRecord]:
        """
        Pure-Python threshold evaluation for replay (no C++ dependency).
        Mirrors the ThresholdDetector logic for correctness comparison.
        """
        effects: list[EffectRecord] = []

        for stage in pipeline.get("stages", []):
            ptype  = stage.get("processor_type", "")
            config = stage.get("config", {})

            if ptype == "threshold" and config.get("metric_name") == metric_name:
                threshold  = float(config.get("threshold", 0))
                if value > threshold:
                    effects.append(EffectRecord(
                        effect_type   = EffectType.NOTIFY,
                        pipeline_id   = pipeline_id,
                        pipeline_name = pipeline.get("name", pipeline_id),
                        details       = {
                            "title":    f"Alert: {metric_name} threshold exceeded",
                            "body":     f"[{source}] {metric_name} = {value:.2f} (threshold: {threshold})",
                            "severity": config.get("severity", "warning"),
                        },
                    ))

        return effects
