"""
Pydantic models for the Meridian REST API.
All request/response bodies are typed here for validation and OpenAPI docs.
"""
from __future__ import annotations

from enum import Enum
from typing import Any, Optional
from pydantic import BaseModel, Field
import time


# ── Enums ─────────────────────────────────────────────────────────────────────

class RuntimeMode(str, Enum):
    SIMULATION = "simulation"
    SHADOW     = "shadow"
    PRODUCTION = "production"


class ProcessorType(str, Enum):
    THRESHOLD  = "threshold"
    ANOMALY    = "anomaly"
    RATE_LIMIT = "rate_limit"
    AGGREGATOR = "aggregator"
    LOG_FILTER = "log_filter"
    HEARTBEAT  = "heartbeat"


class EffectType(str, Enum):
    NOTIFY   = "notify"
    STORE    = "store"
    SUPPRESS = "suppress"
    FORWARD  = "forward"
    NONE     = "none"


# ── Processor configs ──────────────────────────────────────────────────────────

class ThresholdConfig(BaseModel):
    metric_name:    str
    threshold:      float
    hysteresis:     float = 0.0
    severity:       str   = "warning"
    notify_channel: str   = "#alerts"


class AnomalyConfig(BaseModel):
    metric_name:  str
    window_size:  int   = 20
    z_threshold:  float = 2.5
    min_samples:  float = 5.0
    severity:     str   = "warning"


class RateLimitConfig(BaseModel):
    metric_name:    str
    max_per_window: int   = 1
    window_seconds: float = 60.0


class AggregatorConfig(BaseModel):
    metric_name:  str
    flush_every_n: int = 10


# ── Stage / Pipeline ──────────────────────────────────────────────────────────

class PipelineStageSpec(BaseModel):
    label:         str
    processor_type: ProcessorType
    config:        dict[str, Any] = Field(default_factory=dict)


class PipelineCreate(BaseModel):
    name:          str
    mode:          RuntimeMode = RuntimeMode.SIMULATION
    stages:        list[PipelineStageSpec] = Field(default_factory=list)
    stop_on_error: bool = False


class PipelineStageInfo(BaseModel):
    label:           str
    processor_type:  str
    events_processed: int = 0
    errors:          int = 0


class PipelineInfo(BaseModel):
    id:              str
    name:            str
    mode:            RuntimeMode
    stage_count:     int
    stages:          list[PipelineStageInfo] = Field(default_factory=list)
    events_processed: int = 0
    running:         bool = False
    created_at:      float = Field(default_factory=time.time)


# ── Metrics ───────────────────────────────────────────────────────────────────

class MetricPoint(BaseModel):
    timestamp_ms: int
    value:        float


class MetricSeries(BaseModel):
    metric_name: str
    source:      str
    points:      list[MetricPoint] = Field(default_factory=list)


class MetricIngest(BaseModel):
    source:      str
    metric_name: str
    value:       float
    unit:        str = ""
    pipeline_id: Optional[str] = None


# ── Events (WebSocket streaming) ───────────────────────────────────────────────

class StreamEvent(BaseModel):
    event_type:  str
    source:      str
    payload:     dict[str, Any] = Field(default_factory=dict)
    timestamp_ms: int = Field(default_factory=lambda: int(time.time() * 1000))


class EffectRecord(BaseModel):
    effect_type:  EffectType
    pipeline_id:  str
    pipeline_name: str
    details:      dict[str, Any] = Field(default_factory=dict)
    timestamp_ms:  int = Field(default_factory=lambda: int(time.time() * 1000))


# ── Replay ────────────────────────────────────────────────────────────────────

class ReplayScenario(BaseModel):
    name:        str
    description: str = ""
    events:      list[dict[str, Any]] = Field(default_factory=list)
    speed:       float = 1.0  # 1.0 = real-time, 2.0 = 2× faster


class ReplayResult(BaseModel):
    scenario_name:   str
    events_replayed: int
    effects_captured: int
    duration_ms:     float
    effects:         list[EffectRecord] = Field(default_factory=list)


# ── Health ────────────────────────────────────────────────────────────────────

class HealthResponse(BaseModel):
    status:     str = "ok"
    version:    str = "1.0.0"
    uptime_s:   float = 0.0
    pipelines:  int = 0
    events_processed: int = 0
