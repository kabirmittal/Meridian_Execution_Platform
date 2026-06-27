#!/usr/bin/env bash
# demo.sh — one-command local demo
# Requires: cmake, g++, python3, node (or Docker)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[demo]${NC} $*"; }
warn()  { echo -e "${YELLOW}[demo]${NC} $*"; }

# ── C++ ────────────────────────────────────────────────────────────────────────
info "Building C++ core..."
cmake -B core/build -S core -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON -Wno-dev > /dev/null
cmake --build core/build --parallel "$(nproc)" > /dev/null

info "Running C++ examples..."
echo ""
./core/build/anomaly_demo
echo ""
./core/build/multi_pipeline_demo

# ── Python tests ───────────────────────────────────────────────────────────────
echo ""
info "Running Python test suite..."
cd orchestrator
pip install -q -r requirements.txt
python -m pytest tests/ -v --tb=short
cd ..

# ── Start orchestrator ────────────────────────────────────────────────────────
echo ""
info "Starting Meridian orchestrator on http://localhost:8000 ..."
info "  API docs: http://localhost:8000/docs"
info "  Press Ctrl+C to stop"
echo ""
cd orchestrator
uvicorn meridian_orch.api.app:app --host 0.0.0.0 --port 8000 --reload
