"""Tests for metrics ingestion and query endpoints."""
import pytest


@pytest.mark.asyncio
async def test_ingest_metric(client):
    r = await client.post("/metrics/ingest", json={
        "source": "host-01",
        "metric_name": "cpu.usage",
        "value": 72.5,
        "unit": "percent",
    })
    assert r.status_code == 202
    assert r.json()["status"] == "accepted"


@pytest.mark.asyncio
async def test_ingest_batch(client):
    metrics = [
        {"source": "host-01", "metric_name": "cpu.usage",       "value": 45.0},
        {"source": "host-01", "metric_name": "memory.used_pct", "value": 60.0},
        {"source": "host-01", "metric_name": "http.latency_p99","value": 120.0},
    ]
    r = await client.post("/metrics/ingest/batch", json=metrics)
    assert r.status_code == 202
    assert r.json()["count"] == 3


@pytest.mark.asyncio
async def test_list_metrics_after_ingest(client):
    await client.post("/metrics/ingest", json={
        "source": "svc-a", "metric_name": "error.rate", "value": 1.2
    })
    r = await client.get("/metrics")
    assert r.status_code == 200
    assert "svc-a.error.rate" in r.json()


@pytest.mark.asyncio
async def test_query_metric_history(client):
    for v in [10.0, 20.0, 30.0]:
        await client.post("/metrics/ingest", json={
            "source": "db-01", "metric_name": "query_time_ms", "value": v
        })
    r = await client.get("/metrics/db-01.query_time_ms")
    assert r.status_code == 200
    body = r.json()
    assert body["metric_name"] == "query_time_ms"
    assert len(body["points"]) == 3


@pytest.mark.asyncio
async def test_query_nonexistent_metric(client):
    r = await client.get("/metrics/ghost.metric")
    assert r.status_code == 404


@pytest.mark.asyncio
async def test_health_endpoint(client):
    r = await client.get("/health")
    assert r.status_code == 200
    body = r.json()
    assert body["status"] == "ok"
    assert "uptime_s" in body
