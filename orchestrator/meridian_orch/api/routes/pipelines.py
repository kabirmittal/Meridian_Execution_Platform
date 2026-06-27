"""
Pipeline management routes.
CRUD for pipeline definitions + replay trigger.
"""
from __future__ import annotations

import uuid
import time
from typing import Any

from fastapi import APIRouter, HTTPException, Request

from ..models import (
    PipelineCreate, PipelineInfo, PipelineStageInfo,
    ReplayScenario, ReplayResult, RuntimeMode,
)

router = APIRouter(prefix="/pipelines", tags=["pipelines"])


def _registry(request: Request) -> dict[str, Any]:
    return request.app.state.pipelines


# ── CRUD ──────────────────────────────────────────────────────────────────────

@router.get("", response_model=list[PipelineInfo])
async def list_pipelines(request: Request) -> list[PipelineInfo]:
    return list(_registry(request).values())


@router.post("", response_model=PipelineInfo, status_code=201)
async def create_pipeline(body: PipelineCreate, request: Request) -> PipelineInfo:
    pid    = str(uuid.uuid4())[:8]
    stages = [
        PipelineStageInfo(
            label          = s.label,
            processor_type = s.processor_type,
            events_processed = 0,
            errors         = 0,
        )
        for s in body.stages
    ]
    info = PipelineInfo(
        id           = pid,
        name         = body.name,
        mode         = body.mode,
        stage_count  = len(body.stages),
        stages       = stages,
        running      = True,
        created_at   = time.time(),
    )
    # Store raw spec too (for replay engine)
    _registry(request)[pid] = {
        **info.model_dump(),
        "spec": body.model_dump(),
    }
    return info


@router.get("/{pipeline_id}", response_model=PipelineInfo)
async def get_pipeline(pipeline_id: str, request: Request) -> PipelineInfo:
    rec = _registry(request).get(pipeline_id)
    if not rec:
        raise HTTPException(status_code=404, detail=f"Pipeline {pipeline_id} not found")
    return PipelineInfo(**{k: v for k, v in rec.items() if k != "spec"})


@router.delete("/{pipeline_id}", status_code=204)
async def delete_pipeline(pipeline_id: str, request: Request) -> None:
    if pipeline_id not in _registry(request):
        raise HTTPException(status_code=404, detail=f"Pipeline {pipeline_id} not found")
    del _registry(request)[pipeline_id]


# ── Replay ────────────────────────────────────────────────────────────────────

@router.post("/{pipeline_id}/replay", response_model=ReplayResult)
async def replay_pipeline(
    pipeline_id: str,
    scenario:    ReplayScenario,
    request:     Request,
) -> ReplayResult:
    rec = _registry(request).get(pipeline_id)
    if not rec:
        raise HTTPException(status_code=404, detail=f"Pipeline {pipeline_id} not found")

    engine = request.app.state.replay_engine
    result = engine.replay(
        scenario_name    = scenario.name,
        events           = scenario.events,
        pipeline_registry = {pipeline_id: rec.get("spec", rec)},
    )
    return result
