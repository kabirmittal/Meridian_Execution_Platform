"""
Metrics ingestion and query routes.
"""
from __future__ import annotations

import time
from typing import Any

from fastapi import APIRouter, HTTPException, Query, Request

from ..models import MetricIngest, MetricSeries, MetricPoint, EffectRecord

router = APIRouter(prefix="/metrics", tags=["metrics"])


def _store(request: Request):
    return request.app.state.event_store


# ── Ingestion ─────────────────────────────────────────────────────────────────

@router.post("/ingest", status_code=202)
async def ingest_metric(body: MetricIngest, request: Request) -> dict[str, str]:
    key = f"{body.source}.{body.metric_name}"
    _store(request).record_metric(key, body.value)

    # Broadcast to WebSocket clients
    ws_manager = request.app.state.ws_manager
    await ws_manager.broadcast({
        "type":        "metric",
        "source":      body.source,
        "metric_name": body.metric_name,
        "value":       body.value,
        "unit":        body.unit,
        "timestamp_ms": int(time.time() * 1000),
    })
    return {"status": "accepted"}


@router.post("/ingest/batch", status_code=202)
async def ingest_batch(metrics: list[MetricIngest], request: Request) -> dict[str, Any]:
    ws_manager = request.app.state.ws_manager
    for m in metrics:
        key = f"{m.source}.{m.metric_name}"
        _store(request).record_metric(key, m.value)

    await ws_manager.broadcast({
        "type":  "batch_ingest",
        "count": len(metrics),
        "timestamp_ms": int(time.time() * 1000),
    })
    return {"status": "accepted", "count": len(metrics)}


# ── Query ─────────────────────────────────────────────────────────────────────

@router.get("", response_model=list[str])
async def list_metrics(request: Request) -> list[str]:
    return _store(request).list_metrics()


@router.get("/{metric_key}", response_model=MetricSeries)
async def get_metric(
    metric_key: str,
    request:    Request,
    since_ms:   int = Query(default=0),
    limit:      int = Query(default=100, le=1000),
) -> MetricSeries:
    points = _store(request).get_metric_history(metric_key, since_ms=since_ms, limit=limit)
    if not points and since_ms == 0:
        raise HTTPException(status_code=404, detail=f"Metric {metric_key!r} not found")

    parts  = metric_key.split(".", 1)
    source = parts[0] if len(parts) > 1 else "unknown"
    name   = parts[1] if len(parts) > 1 else metric_key

    return MetricSeries(metric_name=name, source=source, points=points)


@router.get("/effects/recent", response_model=list[EffectRecord])
async def get_effects(
    request: Request,
    limit:   int = Query(default=50, le=500),
) -> list[EffectRecord]:
    return _store(request).get_effects(limit=limit)
