#pragma once

#include "runtime.hpp"
#include "runtime_api.hpp"
#include <unordered_map>
#include <vector>
#include <deque>
#include <string>
#include <optional>
#include <chrono>
#include <functional>
#include <stdexcept>

namespace meridian {

// ─── SimRuntimeAPI: in-memory RuntimeAPI used by SimRuntime ──────────────────
// All data comes from pre-seeded maps; all effects are captured for inspection.
// The sim clock advances only when the user calls advanceClock().
// This makes tests fully deterministic and fast (no real I/O, no sleep()).

class SimRuntimeAPI final : public RuntimeAPI {
public:
    // ── Data seeding (set up before running events) ────────────────────────────
    void seedMetric(const std::string& key, double value);
    void seedMetricHistory(const std::string& key, std::vector<double> values);
    // Append a single point without wiping the existing history
    void appendMetricHistory(const std::string& key, double value);
    void seedConfig(const std::string& key, std::string value);

    // ── Clock control ─────────────────────────────────────────────────────────
    void setSimTime(Timestamp t)       { sim_time_ = t; }
    void advanceClock(Duration delta)  { sim_time_ += delta; }
    void advanceClock(std::chrono::seconds s) {
        sim_time_ += std::chrono::duration_cast<Duration>(s);
    }

    // ── Effect inspection ─────────────────────────────────────────────────────
    [[nodiscard]] const std::vector<Effect>& capturedEffects() const { return effects_; }
    [[nodiscard]] size_t effectCount() const { return effects_.size(); }
    [[nodiscard]] size_t effectCount(auto pred) const {
        return static_cast<size_t>(
            std::count_if(effects_.begin(), effects_.end(), pred));
    }

    template<typename EffectT>
    [[nodiscard]] std::vector<EffectT> capturedEffectsOf() const {
        std::vector<EffectT> out;
        for (const auto& e : effects_)
            if (const auto* p = std::get_if<EffectT>(&e)) out.push_back(*p);
        return out;
    }

    void clearEffects() { effects_.clear(); }

    // ── RuntimeAPI interface ───────────────────────────────────────────────────
    [[nodiscard]] std::optional<double> queryMetric(std::string_view key) const override;
    [[nodiscard]] std::vector<double>   queryMetricHistory(
        std::string_view key, std::chrono::seconds window) const override;
    [[nodiscard]] Timestamp now() const override { return sim_time_; }
    [[nodiscard]] std::optional<std::string> getConfig(std::string_view key) const override;
    [[nodiscard]] std::optional<std::string> getState(std::string_view key) const override;
    void setState(std::string_view key, std::string value) override;
    void clearState(std::string_view key) override;
    void emit(Effect effect) override;

private:
    std::unordered_map<std::string, double>              metrics_;
    std::unordered_map<std::string, std::vector<double>> history_;
    std::unordered_map<std::string, std::string>         config_;
    std::unordered_map<std::string, std::string>         state_;
    std::vector<Effect>                                  effects_;
    Timestamp sim_time_{Clock::now()};
};

// ─── SimRuntime: synchronous, deterministic runtime ───────────────────────────
//
// Event injection is synchronous — injectEvent() processes the event
// through all registered pipelines before returning to the caller.
// There are no worker threads, no queues, and no real I/O.
//
// Perfect for:
//   • Unit testing processors
//   • Replay of recorded event logs
//   • Backtesting / simulation scenarios
//   • CI/CD validation of business logic

class SimRuntime final : public Runtime {
public:
    SimRuntime() : api_(std::make_unique<SimRuntimeAPI>()) {}
    explicit SimRuntime(RuntimeConfig cfg)
        : config_(cfg), api_(std::make_unique<SimRuntimeAPI>()) {}

    // ── Pipeline registration ──────────────────────────────────────────────────
    void registerPipeline(std::shared_ptr<Pipeline> pipeline) override;

    // ── Event injection ────────────────────────────────────────────────────────
    // Synchronous: processes through all pipelines before returning.
    void injectEvent(Event event) override;
    void injectBatch(std::vector<Event> events) override;

    // ── Lifecycle ──────────────────────────────────────────────────────────────
    void start() override;
    void stop()  override;
    [[nodiscard]] bool isRunning() const override { return running_; }

    // ── Effect handlers ────────────────────────────────────────────────────────
    void registerEffectHandler(EffectHandler handler) override {
        effect_handlers_.push_back(std::move(handler));
    }

    // ── Metrics ────────────────────────────────────────────────────────────────
    [[nodiscard]] RuntimeMetrics getMetrics() const override;
    [[nodiscard]] RuntimeMode    mode()       const override {
        return RuntimeMode::SIMULATION;
    }

    // ── SimRuntime-specific API ────────────────────────────────────────────────
    [[nodiscard]] SimRuntimeAPI& api()              { return *api_; }
    [[nodiscard]] const SimRuntimeAPI& api() const  { return *api_; }

    // Convenience: process a replay of event vectors
    void replay(const std::vector<Event>& events);

    // Drain pending tick callbacks if any timers have fired
    void tick();

private:
    void dispatchEffects(const std::vector<Effect>& effects);

    RuntimeConfig                          config_;
    std::unique_ptr<SimRuntimeAPI>         api_;
    std::vector<std::shared_ptr<Pipeline>> pipelines_;
    std::vector<EffectHandler>             effect_handlers_;
    bool                                   running_{false};

    // Metrics
    uint64_t events_processed_{0};
    uint64_t effects_emitted_{0};
    uint64_t events_suppressed_{0};
    uint64_t processor_errors_{0};
};

} // namespace meridian
