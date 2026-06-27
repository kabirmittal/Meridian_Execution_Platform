#pragma once

#include "runtime.hpp"
#include "runtime_api.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <vector>
#include <memory>

namespace meridian {

// ─── ProdRuntimeAPI: real RuntimeAPI used by ProdRuntime ─────────────────────
// Data comes from an in-memory time-series store populated by ingested events.
// Effects are dispatched to registered EffectHandlers.

class ProdRuntimeAPI final : public RuntimeAPI {
public:
    // Runtime state store (for processor state persistence)
    using StateStore = std::unordered_map<std::string, std::string>;

    void ingestMetric(const std::string& key, double value, Timestamp ts);

    // ── RuntimeAPI interface ──────────────────────────────────────────────────
    [[nodiscard]] std::optional<double> queryMetric(std::string_view key) const override;
    [[nodiscard]] std::vector<double>   queryMetricHistory(
        std::string_view key, std::chrono::seconds window) const override;
    [[nodiscard]] Timestamp now() const override { return Clock::now(); }
    [[nodiscard]] std::optional<std::string> getConfig(std::string_view key) const override;
    [[nodiscard]] std::optional<std::string> getState(std::string_view key) const override;
    void setState(std::string_view key, std::string value) override;
    void clearState(std::string_view key) override;
    void emit(Effect effect) override;

    // Called by ProdRuntime to set up effect dispatch
    void setEffectDispatcher(std::function<void(Effect)> dispatcher);
    void setConfig(std::string key, std::string value);

private:
    struct TimedSample { double value; Timestamp ts; };

    mutable std::mutex                                    data_mutex_;
    std::unordered_map<std::string, std::deque<TimedSample>> history_;
    std::unordered_map<std::string, std::string>          config_;
    StateStore                                            state_;
    std::function<void(Effect)>                           effect_dispatcher_;
};

// ─── ProdRuntime: async queue-based production runtime ────────────────────────
//
// Architecture:
//   Producer threads → injectEvent() → lock-free bounded queue
//   → single dispatcher thread → pipeline.process() → effect dispatch
//
// The event queue is bounded (config.queue_capacity) to apply backpressure.
// A tick thread fires pipeline.onTick() at config.tick_interval.
//
// Shadow mode (RuntimeMode::SHADOW):
//   Events are processed identically; effects ARE emitted into the API but
//   effect handlers are NOT called. Used for dark-launch validation.

class ProdRuntime final : public Runtime {
public:
    explicit ProdRuntime(RuntimeConfig config = {});
    ~ProdRuntime() override;

    // Non-copyable, non-movable (owns threads)
    ProdRuntime(const ProdRuntime&) = delete;
    ProdRuntime& operator=(const ProdRuntime&) = delete;

    // ── Pipeline registration ──────────────────────────────────────────────────
    void registerPipeline(std::shared_ptr<Pipeline> pipeline) override;

    // ── Event injection ────────────────────────────────────────────────────────
    // Enqueues the event; blocks if queue is full (backpressure)
    void injectEvent(Event event) override;
    void injectBatch(std::vector<Event> events) override;

    // ── Lifecycle ──────────────────────────────────────────────────────────────
    void start() override;
    void stop()  override;
    [[nodiscard]] bool isRunning() const override {
        return running_.load(std::memory_order_acquire);
    }

    // ── Effect handlers ────────────────────────────────────────────────────────
    void registerEffectHandler(EffectHandler handler) override {
        std::lock_guard<std::mutex> lk(handler_mutex_);
        effect_handlers_.push_back(std::move(handler));
    }

    // ── Metrics ────────────────────────────────────────────────────────────────
    [[nodiscard]] RuntimeMetrics getMetrics() const override;
    [[nodiscard]] RuntimeMode    mode()       const override { return config_.mode; }

    // ── ProdRuntime-specific API ───────────────────────────────────────────────
    [[nodiscard]] ProdRuntimeAPI& api()             { return *api_; }
    [[nodiscard]] size_t          queueDepth() const;

private:
    void dispatchLoop();    // worker thread: drains event queue, runs pipelines
    void tickLoop();        // tick thread: fires onTick() on pipelines
    void dispatchEffects(const std::vector<Effect>& effects, bool shadow);

    RuntimeConfig                          config_;
    std::unique_ptr<ProdRuntimeAPI>        api_;
    std::vector<std::shared_ptr<Pipeline>> pipelines_;

    // Event queue (bounded)
    std::queue<Event>        event_queue_;
    mutable std::mutex       queue_mutex_;
    std::condition_variable  queue_cv_;

    // Effect handlers
    std::vector<EffectHandler> effect_handlers_;
    mutable std::mutex         handler_mutex_;

    // Worker threads
    std::jthread dispatch_thread_;
    std::jthread tick_thread_;

    std::atomic<bool>     running_{false};
    std::atomic<uint64_t> events_processed_{0};
    std::atomic<uint64_t> events_dropped_{0};
    std::atomic<uint64_t> effects_emitted_{0};
    std::atomic<uint64_t> processor_errors_{0};
};

} // namespace meridian
