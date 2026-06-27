"""
Meridian Orchestrator
~~~~~~~~~~~~~~~~~~~~~
FastAPI-based management layer for the Meridian event-driven platform.

Provides:
  - REST API for pipeline CRUD and metric queries
  - WebSocket endpoint for real-time event streaming
  - Synthetic event generator for demos
  - Replay engine for deterministic scenario testing
"""

__version__ = "1.0.0"
__all__ = ["create_app"]

from .api.app import create_app
