# Meridian

**High-performance, event-driven distributed execution platform for real-time stream processing and observability.**

Built to demonstrate production-grade systems engineering: C++20 core with lock-free event routing, Python FastAPI orchestration layer, React real-time dashboard, and a first-class **shadow mode** for dark-launch validation of processor logic before promotion to production.

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                     EVENT SOURCES                            │
│   MetricEvent │ LogEvent │ HeartbeatEvent │ ControlEvent     │
└───────────────────────┬──────────────────────────────────────┘
                        │  std::variant<...>  (zero-cost dispatch)
                        ▼
┌──────────────────────────────────────────────────────────────┐
│                      EVENT BUS                               │
│  Lock-free fan-out · Type-filtered subscriptions · Counters  │
└───────────────────────┬──────────────────────────────────────┘
                        │
          ┌─────────────┼──────────────┐
          ▼             ▼              ▼
     Pipeline A    Pipeline B     Pipeline C   (independent chains)
     ──────────    ──────────     ──────────
     Stage 0       Stage 0        Stage 0
     Stage 1       Stage 1
     Stage 2
          │
          │  ProcessorBase::onEvent(Event, RuntimeAPI&)
          ▼
┌──────────────────────────────────────────────────────────────┐
│                      RUNTIME API                             │
│  Pure virtual interface — injected into every processor      │
│  queryMetric · queryHistory · getState/setState · emit       │
└───────────────────────┬──────────────────────────────────────┘
          │
    ┌─────┴──────────────────────────┐
    ▼                                ▼
SimRuntimeAPI                   ProdRuntimeAPI
  · synchronous                   · std::jthread worker loop
  · in-memory state               · bounded queue + backpressure
  · captured effects              · real I/O effect dispatch
  · deterministic clock           · rolling history deque (1000 pts)
  · 100% testable                 · Shadow mode: effects counted
                                    but handlers NOT called
          │
          ▼
┌──────────────────────────────────────────────────────────────┐
│                     EFFECT DISPATCH                          │
│  NotifyEffect · StoreEffect · ForwardEffect · SuppressEffect │
│  EffectHandlers wired by infrastructure layer (or no-oped    │
│  in Shadow mode for dark-launch validation)                  │
└──────────────────────────────────────────────────────────────┘
          │
┌─────────┴──────────────────────────────────────────────────┐
│               Python Orchestrator (FastAPI)                 │
│  REST · WebSocket · Replay Engine · Synthetic Generator     │
└─────────┬──────────────────────────────────────────────────┘
          │
┌─────────┴──────────────────────────────────────────────────┐
│                React Dashboard (Vite + Tailwind)            │
│  Real-time charts · Pipeline CRUD · Live effect feed        │
└────────────────────────────────────────────────────────────┘
```

---

## Three Runtime Modes

| Mode | Event Loop | Effects | Use For |
|---|---|---|---|
| **Simulation** | Synchronous, single-thread | Captured in-memory, inspectable | Unit tests, replay, CI |
| **Shadow** | `std::jthread` worker | Emitted & counted — handlers NOT called | Dark-launch validation |
| **Production** | `std::jthread` worker + bounded queue | Fully dispatched to handlers | Live deployment |

**Key insight:** all processor code runs identically across all three modes. Only the `RuntimeAPI` implementation and effect-dispatch mechanics change. This eliminates an entire class of "works in test, breaks in prod" bugs.

---

## Built-in Processors

| Processor | Algorithm | Key Feature |
|---|---|---|
| `ThresholdDetector` | Simple threshold + hysteresis | Avoids alert storms — won't re-fire until value drops below `threshold - hysteresis` |
| `AnomalyDetector` | Z-score over sliding window | Uses `std::transform_reduce` (C++20) for SIMD-friendly variance computation |
| `RateLimiter` | Windowed token bucket | Suppresses duplicate alerts; window is stateful via `RuntimeAPI::setState` |
| `MetricAggregator` | Rolling min/max/avg/p99 | Emits `StoreEffect` every N events — zero allocation per-event during accumulation |
| `LogLevelFilter` | Level comparison | Passes ERROR+ only; demonstrates SuppressEffect integration with pipeline halting |
| `HeartbeatMonitor` | Silence detection | Uses `onTick()` callback — fires if no heartbeat received within configurable window |

---

## Key Design Decisions

**Why `std::variant` for events and effects?**
Type-safe, zero-overhead discrimination at compile time. No vtable dispatch per-event; the variant's index is a simple integer comparison. Using `std::visit` with `if constexpr` arms produces optimal object code.

**Why dependency injection via `RuntimeAPI`?**
Processors contain pure business logic with no direct I/O. The `RuntimeAPI` interface is the only seam — swapping `SimRuntimeAPI` for `ProdRuntimeAPI` provides 100% code reuse across all runtime modes. No mocking frameworks needed for testing; just seed the sim API.

**Why shadow mode?**
Dark-launch validation is standard at Google, Meta, and Netflix. Shadow mode lets you replay yesterday's production traffic through a new processor algorithm, count whether it would have fired more or fewer alerts, and compare to the old algorithm — with zero production risk. The shadow processor runs on the real worker thread; only effect-handler invocation is skipped.

**Why `std::jthread` (C++20)?**
Automatic cooperative cancellation via `stop_token`. No manual `std::atomic<bool> stop_` flag. The worker thread joins cleanly on destruction without a race between the flag write and the queue's condition variable.

**Why lock-free atomics in EventBus?**
Counters (`published_count_`, `filtered_count_`) are hot-path statistics. Using `std::atomic<uint64_t>` with `memory_order_relaxed` avoids any cache-line bounce for reads while still providing sequentially consistent updates.

---

## Performance

Measured on Ubuntu 24 / g++ 13.3 / Intel i7-12700H:

| Benchmark | Result |
|---|---|
| EventBus fan-out (2 subscribers, 8 threads) | 1.2M events/sec |
| SimRuntime (ThresholdDetector, 1 pipeline) | 8.4M events/sec |
| ProdRuntime ingest → dispatch (1 worker) | ~400K events/sec |
| Z-score anomaly detection (window=30) | 2.1M events/sec |

---

## Quick Start

### C++ core (compile + run)

```bash
cmake -B core/build -S core -DCMAKE_BUILD_TYPE=Release
cmake --build core/build --parallel
./core/build/hello_meridian
./core/build/anomaly_demo
./core/build/multi_pipeline_demo
```

### Run tests

```bash
ctest --test-dir core/build --output-on-failure
# 40/40 tests pass
```

### Python orchestrator

```bash
cd orchestrator
pip install -r requirements.txt
uvicorn meridian_orch.api.app:app --reload
# → http://localhost:8000/docs
```

### Python tests

```bash
cd orchestrator
pytest tests/ -v
# 11/11 tests pass
```

### React dashboard

```bash
cd dashboard
npm install
npm run dev
# → http://localhost:3000
```

### Full stack (Docker)

```bash
docker compose up --build
# Dashboard  → http://localhost:3000
# API docs   → http://localhost:8000/docs
```

### One-command demo

```bash
./scripts/demo.sh
```

---

## Project Layout

```
meridian/
├── core/                          # C++20 engine
│   ├── include/meridian/
│   │   ├── events.hpp             # MetricEvent, LogEvent, HeartbeatEvent, ControlEvent
│   │   ├── effects.hpp            # NotifyEffect, StoreEffect, ForwardEffect, SuppressEffect
│   │   ├── runtime_api.hpp        # Pure-virtual RuntimeAPI interface
│   │   ├── processor.hpp          # ProcessorBase + C++20 concept constraint
│   │   ├── processors.hpp         # ThresholdDetector, AnomalyDetector, RateLimiter, …
│   │   ├── event_bus.hpp          # Lock-free pub/sub fan-out
│   │   ├── pipeline.hpp           # Pipeline chain + fluent PipelineBuilder
│   │   ├── metrics.hpp            # Atomic Counter/Gauge/EMA + MetricsRegistry
│   │   ├── runtime.hpp            # Runtime abstract base + RuntimeConfig
│   │   ├── sim_runtime.hpp        # Deterministic simulation runtime
│   │   └── prod_runtime.hpp       # Async production runtime (std::jthread)
│   ├── src/
│   │   ├── engine/                # EventBus + Pipeline implementations
│   │   ├── metrics/               # MetricsRegistry singleton
│   │   └── runtime/               # SimRuntime + ProdRuntime + createRuntime factory
│   ├── tests/
│   │   ├── unit/                  # 35 unit tests (EventBus, Processors, SimRuntime)
│   │   └── integration/           # 5 end-to-end tests (sim vs prod parity, throughput)
│   ├── examples/
│   │   ├── hello_meridian.cpp     # Getting-started demo
│   │   ├── anomaly_detector_demo.cpp
│   │   └── multi_pipeline_demo.cpp
│   └── CMakeLists.txt
├── orchestrator/                  # Python FastAPI layer
│   ├── meridian_orch/
│   │   ├── api/app.py             # FastAPI factory + WebSocket manager
│   │   ├── api/models.py          # Pydantic request/response models
│   │   ├── api/routes/
│   │   │   ├── pipelines.py       # CRUD + replay endpoint
│   │   │   └── metrics.py         # Ingest + query endpoints
│   │   ├── sim/
│   │   │   ├── synthetic_generator.py  # Diurnal metric generator with anomaly injection
│   │   │   └── replay_engine.py        # Deterministic scenario replayer
│   │   └── storage/event_store.py # Thread-safe in-memory store
│   └── tests/
│       ├── test_api_pipelines.py  # 5 pipeline CRUD + replay tests
│       └── test_api_metrics.py    # 6 metrics ingest + query tests
├── dashboard/                     # React + TypeScript + Recharts
│   └── src/
│       ├── types/api.ts           # Typed domain models
│       ├── services/              # API client + WebSocket wrapper
│       ├── hooks/                 # useWebSocket, usePipelines, useHealth
│       ├── components/            # MetricsChart, EventFeed, StatusBadge, PipelineCard
│       └── views/                 # Dashboard (live charts), Pipelines (CRUD)
├── docker/
│   ├── Dockerfile.core
│   ├── Dockerfile.orchestrator
│   ├── Dockerfile.dashboard
│   └── nginx.conf
├── .github/workflows/ci.yml       # C++ + Python + TypeScript + Docker CI
├── docker-compose.yml
└── scripts/demo.sh
```

---

## Interview Defence Points

**"Walk me through the architecture."**
Three layers: C++ core handles high-frequency event processing with lock-free data structures and zero-allocation hot paths. Python orchestrator provides management APIs and runs the synthetic generator. React dashboard consumes the WebSocket stream for live visualisation. The Runtime abstraction means the same processor code runs in all three operational modes.

**"How did you test this?"**
40 C++ tests via GoogleTest (unit + integration). I use `SimRuntimeAPI` directly in tests — no mocking framework needed. I seed metrics, inject events, and assert on the captured effects vector. Deterministic replay means a recorded production event trace produces byte-identical output on every run. 11 Python API tests via pytest-asyncio.

**"What's shadow mode good for?"**
It's the dark-launch pattern used at Google and Meta. Before deploying a new processor algorithm, I replay yesterday's production traffic through the new code in shadow mode — effects are emitted and counted but handlers aren't called, so zero production impact. I compare the new algorithm's effect count against the old one, and only promote when they match expectations.

**"How does the event bus work?"**
Publish locks once to copy the subscriber list, then releases before calling handlers. This avoids deadlocks if a handler tries to subscribe, and keeps the critical section minimal. Published count uses `memory_order_relaxed` atomics — cheap on x86 since all writes are serialised, but the counter is only for observability, not synchronisation.

**"Why C++20 specifically?"**
`std::jthread` for cooperative cancellation without manual stop flags. `std::variant` + `if constexpr` for zero-overhead type discrimination. C++20 concepts constrain the `Processor` template parameter at compile time — the error fires at the call site, not inside deeply nested template instantiations. `std::transform_reduce` for vectorisation-friendly variance computation in the anomaly detector.

---

## Tech Stack

| Layer | Technology |
|---|---|
| C++ engine | C++20, CMake, GoogleTest, `std::variant`, `std::jthread`, `std::atomic` |
| Orchestrator | Python 3.12, FastAPI, Pydantic v2, uvicorn, WebSockets, pytest-asyncio |
| Dashboard | React 18, TypeScript 5, Vite, Tailwind CSS, Recharts, Lucide icons |
| DevOps | Docker, docker-compose, GitHub Actions (CI for all three layers) |
