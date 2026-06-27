"""Shared pytest fixtures — manually boot app state so lifespan runs."""
import pytest
import pytest_asyncio
from httpx import AsyncClient, ASGITransport
from meridian_orch.api.app import create_app
from meridian_orch.storage.event_store import EventStore
from meridian_orch.sim.replay_engine import ReplayEngine
from meridian_orch.api.app import ConnectionManager


@pytest.fixture
def app():
    application = create_app(enable_synthetic_generator=False)
    # Bootstrap state manually (lifespan not invoked by ASGITransport)
    application.state.event_store   = EventStore()
    application.state.ws_manager    = ConnectionManager()
    application.state.replay_engine = ReplayEngine()
    application.state.pipelines     = {}
    application.state.generator     = None
    return application


@pytest_asyncio.fixture
async def client(app):
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as c:
        yield c
