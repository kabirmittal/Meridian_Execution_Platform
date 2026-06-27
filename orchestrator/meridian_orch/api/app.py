"""
Meridian Orchestrator — FastAPI application factory.

Architecture:
  POST /pipelines          → create / configure a pipeline
  GET  /pipelines          → list all pipelines
  POST /pipelines/:id/replay → deterministic replay against a scenario
  POST /metrics/ingest     → push a MetricEvent into registered pipelines
  GET  /metrics/:key       → query historical metric data
  GET  /metrics/effects/recent → last N effects across all pipelines
  WS   /ws                 → real-time event stream (JSON frames)
  GET  /health             → service health + counters
"""
from __future__ import annotations

import asyncio
import json
import time
from contextlib import asynccontextmanager
from typing import Any

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware

from .models import HealthResponse
from .routes.pipelines import router as pipeline_router
from .routes.metrics import router as metrics_router
from ..sim.synthetic_generator import SyntheticGenerator, GeneratorConfig
from ..sim.replay_engine import ReplayEngine
from ..storage.event_store import EventStore


# ── WebSocket connection manager ───────────────────────────────────────────────

class ConnectionManager:
    def __init__(self) -> None:
        self._clients: list[WebSocket] = []

    async def connect(self, ws: WebSocket) -> None:
        await ws.accept()
        self._clients.append(ws)

    def disconnect(self, ws: WebSocket) -> None:
        if ws in self._clients:
            self._clients.remove(ws)

    async def broadcast(self, data: dict[str, Any]) -> None:
        dead: list[WebSocket] = []
        for ws in list(self._clients):
            try:
                await ws.send_text(json.dumps(data))
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.disconnect(ws)

    @property
    def connection_count(self) -> int:
        return len(self._clients)


# ── App factory ───────────────────────────────────────────────────────────────

def create_app(
    *,
    enable_synthetic_generator: bool = True,
    generator_interval_ms: float = 500.0,
) -> FastAPI:

    start_time = time.time()
    total_events: list[int] = [0]  # mutable container for closure

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        # Startup
        app.state.event_store  = EventStore()
        app.state.ws_manager   = ConnectionManager()
        app.state.replay_engine = ReplayEngine()
        app.state.pipelines    = {}  # id → pipeline record
        app.state.generator    = None

        if enable_synthetic_generator:
            gen = SyntheticGenerator(GeneratorConfig(interval_ms=generator_interval_ms))
            app.state.generator = gen

            async def _run_generator():
                async for metric in gen.stream():
                    key = f"{metric.source}.{metric.metric_name}"
                    app.state.event_store.record_metric(key, metric.value)
                    total_events[0] += 1
                    await app.state.ws_manager.broadcast({
                        "type":        "metric",
                        "source":      metric.source,
                        "metric_name": metric.metric_name,
                        "value":       round(metric.value, 3),
                        "unit":        metric.unit,
                        "timestamp_ms": int(time.time() * 1000),
                    })

            asyncio.create_task(_run_generator())

        yield  # App is running

        # Shutdown
        if app.state.generator:
            app.state.generator.stop()

    app = FastAPI(
        title="Meridian Orchestrator",
        description=(
            "REST + WebSocket management API for the Meridian "
            "event-driven execution platform."
        ),
        version="1.0.0",
        lifespan=lifespan,
    )

    # CORS — allow the Vite dev server and any Docker network
    app.add_middleware(
        CORSMiddleware,
        allow_origins=["*"],
        allow_methods=["*"],
        allow_headers=["*"],
    )

    # ── Routers ───────────────────────────────────────────────────────────────
    app.include_router(pipeline_router)
    app.include_router(metrics_router)

    # ── WebSocket ─────────────────────────────────────────────────────────────
    @app.websocket("/ws")
    async def websocket_endpoint(ws: WebSocket):
        mgr = app.state.ws_manager
        await mgr.connect(ws)
        # Send welcome frame
        await ws.send_text(json.dumps({
            "type":    "connected",
            "message": "Meridian real-time event stream",
            "clients": mgr.connection_count,
        }))
        try:
            while True:
                # Keep connection alive; client messages are ignored for now
                await ws.receive_text()
        except WebSocketDisconnect:
            mgr.disconnect(ws)

    # ── Health ────────────────────────────────────────────────────────────────
    @app.get("/health", response_model=HealthResponse, tags=["health"])
    async def health() -> HealthResponse:
        return HealthResponse(
            status           = "ok",
            version          = "1.0.0",
            uptime_s         = round(time.time() - start_time, 1),
            pipelines        = len(app.state.pipelines),
            events_processed = total_events[0],
        )

    @app.get("/", include_in_schema=False)
    async def root():
        return {"service": "meridian-orchestrator", "docs": "/docs"}

    return app


# ── Entry point ───────────────────────────────────────────────────────────────
app = create_app()
