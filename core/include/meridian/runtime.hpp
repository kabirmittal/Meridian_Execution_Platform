#pragma once

#include "pipeline.hpp"
#include "metrics.hpp"
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <chrono>

namespace meridian {

// ─── Runtime modes ────────────────────────────────────────────────────────────
enum class RuntimeMode : uint8_t {
    SIMULATION,  // Synchronous, deterministic, no real I/O — for testing/replay
    SHADOW,      // Async, real events, effects captured but NOT executed — dark launch
    PRODUCTION   // Async, real events, effects fully executed — live deployment
};

[[nodiscard]] inline std::string_view runtimeModeName(RuntimeMode m) {
    switch (m) {
        case RuntimeMode::SIMULATION:  return "SIMULATION";
        case RuntimeMode::SHADOW:      return "SHADOW";
        case RuntimeMode::PRODUCTION:  return "PRODUCTION";
    }
    return "UNKNOWN";
}

// ─── RuntimeConfig ────────────────────────────────────────────────────────────
struct RuntimeConfig {
    RuntimeMode mode{RuntimeMode::SIMULATION};
    size_t  worker_threads{1};           // ProdRuntime: number of dispatch workers
    size_t  queue_capacity{4096};        // ProdRuntime: event queue depth
    std::chrono::milliseconds tick_interval{1000};  // Interval for onTick() calls
    bool    enable_metrics{true};        // Wire up MetricsRegistry instrumentation
    bool    log_effects{false};          // Log every emitted effect (debug aid)
};

// ─── Runtime: the wiring layer ────────────────────────────────────────────────
//
// The Runtime owns:
//   • Registered pipelines (business logic)
//   • RuntimeAPI implementations (data + effect dispatch)
//   • The event loop (synchronous in sim; async queue+thread in prod)
//
// Invariant: all pipelines run identically regardless of RuntimeMode.
// Only the RuntimeAPI implementation and event-dispatch mechanics differ.

class Runtime {
public:
    virtual ~Runtime() = default;

    // ── Pipeline registration ─────────────────────────────────────────────────
    virtual void registerPipeline(std::shared_ptr<Pipeline> pipeline) = 0;

    // ── Event injection ───────────────────────────────────────────────────────
    // In SimRuntime:  processes synchronously before returning
    // In ProdRuntime: enqueues; returns after enqueue (may block if queue full)
    virtual void injectEvent(Event event) = 0;

    // Inject a batch of events in order (efficient; no locking between items)
    virtual void injectBatch(std::vector<Event> events) = 0;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    virtual void start() = 0;
    virtual void stop()  = 0;
    [[nodiscard]] virtual bool isRunning() const = 0;

    // ── Effect handlers (infrastructure layer) ────────────────────────────────
    // The infrastructure layer registers these to execute NotifyEffect, StoreEffect, etc.
    // In ShadowRuntime they are registered but calls are intercepted/no-oped.
    using EffectHandler = std::function<void(const Effect&)>;
    virtual void registerEffectHandler(EffectHandler handler) = 0;

    // ── Metrics ───────────────────────────────────────────────────────────────
    [[nodiscard]] virtual RuntimeMetrics getMetrics() const = 0;

    [[nodiscard]] virtual RuntimeMode mode() const = 0;
};

// ─── Factory ──────────────────────────────────────────────────────────────────
[[nodiscard]] std::unique_ptr<Runtime> createRuntime(RuntimeConfig config = {});

} // namespace meridian
