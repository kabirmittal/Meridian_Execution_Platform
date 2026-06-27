"""Tests for pipeline CRUD and replay endpoints."""
import pytest


@pytest.mark.asyncio
async def test_list_pipelines_empty(client):
    r = await client.get("/pipelines")
    assert r.status_code == 200
    assert r.json() == []


@pytest.mark.asyncio
async def test_create_pipeline(client):
    payload = {
        "name": "cpu-watch",
        "mode": "simulation",
        "stages": [
            {
                "label": "threshold",
                "processor_type": "threshold",
                "config": {"metric_name": "cpu.usage", "threshold": 80.0},
            }
        ],
    }
    r = await client.post("/pipelines", json=payload)
    assert r.status_code == 201
    body = r.json()
    assert body["name"] == "cpu-watch"
    assert body["stage_count"] == 1
    assert "id" in body


@pytest.mark.asyncio
async def test_get_pipeline(client):
    create = await client.post("/pipelines", json={
        "name": "test", "mode": "simulation", "stages": []
    })
    pid = create.json()["id"]
    r = await client.get(f"/pipelines/{pid}")
    assert r.status_code == 200
    assert r.json()["id"] == pid


@pytest.mark.asyncio
async def test_delete_pipeline(client):
    create = await client.post("/pipelines", json={
        "name": "ephemeral", "mode": "simulation", "stages": []
    })
    pid = create.json()["id"]
    r = await client.delete(f"/pipelines/{pid}")
    assert r.status_code == 204
    r2 = await client.get(f"/pipelines/{pid}")
    assert r2.status_code == 404


@pytest.mark.asyncio
async def test_replay_pipeline(client):
    create = await client.post("/pipelines", json={
        "name": "replay-test",
        "mode": "simulation",
        "stages": [
            {
                "label": "thresh",
                "processor_type": "threshold",
                "config": {"metric_name": "cpu.usage", "threshold": 70.0},
            }
        ],
    })
    pid = create.json()["id"]

    scenario = {
        "name": "spike-scenario",
        "events": [
            {"source": "h1", "metric_name": "cpu.usage", "value": 50.0},
            {"source": "h1", "metric_name": "cpu.usage", "value": 85.0},  # Should fire
            {"source": "h1", "metric_name": "cpu.usage", "value": 60.0},
        ],
    }
    r = await client.post(f"/pipelines/{pid}/replay", json=scenario)
    assert r.status_code == 200
    body = r.json()
    assert body["events_replayed"] == 3
    assert body["effects_captured"] >= 1  # At least the threshold crossing
