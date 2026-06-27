#pragma once

#include "events.hpp"
#include "effects.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <functional>

namespace meridian {

// ─── RuntimeAPI: dependency-injected interface for all Processors ─────────────
//
// This is the ONLY way processors interact with the outside world.
// They never call real I/O directly. The runtime provides a concrete
// implementation of RuntimeAPI that routes:
//   - queryMetric()      → SimRuntime: in-memory map | ProdRuntime: real DB
//   - emit()             → SimRuntime: vector capture | ProdRuntime: dispatcher
//   - now()              → SimRuntime: sim clock     | ProdRuntime: wall clock
//   - getState/setState  → SimRuntime: in-memory map | ProdRuntime: Redis/etc.
//
// This pattern enables 100% business-logic reuse across all runtime modes.

class RuntimeAPI {
public:
    virtual ~RuntimeAPI() = default;

    // ── Data access ──────────────────────────────────────────────────────────

    // Get the latest value for a named metric (if it exists)
    [[nodiscard]] virtual std::optional<double> queryMetric(
        std::string_view key) const = 0;

    // Get a sliding window of values for a named metric
    [[nodiscard]] virtual std::vector<double> queryMetricHistory(
        std::string_view key,
        std::chrono::seconds window) const = 0;

    // Current time (real wall clock in prod; deterministic sim-clock in sim mode)
    [[nodiscard]] virtual Timestamp now() const = 0;

    // ── Configuration ─────────────────────────────────────────────────────────
    [[nodiscard]] virtual std::optional<std::string> getConfig(
        std::string_view key) const = 0;

    // ── Processor-local state (persisted between events) ─────────────────────
    [[nodiscard]] virtual std::optional<std::string> getState(
        std::string_view key) const = 0;
    virtual void setState(std::string_view key, std::string value) = 0;
    virtual void clearState(std::string_view key) = 0;

    // ── Effect emission ────────────────────────────────────────────────────────
    // Emit a single effect (side-effect-free from the processor's perspective)
    virtual void emit(Effect effect) = 0;

    // Emit a batch of effects
    void emitAll(Effects effects) {
        for (auto& e : effects) emit(std::move(e));
    }

    // ── Helpers built on the above primitives ─────────────────────────────────

    // Convenience: read a double config value with a default
    [[nodiscard]] double configDouble(std::string_view key, double def = 0.0) const {
        auto v = getConfig(key);
        return v ? std::stod(*v) : def;
    }

    // Convenience: read a state value as double
    [[nodiscard]] std::optional<double> stateDouble(std::string_view key) const {
        auto v = getState(key);
        return v ? std::make_optional(std::stod(*v)) : std::nullopt;
    }

    // Convenience: set a double state value
    void setStateDouble(std::string_view key, double value) {
        setState(key, std::to_string(value));
    }

    // Convenience: increment a named counter in state
    void incrementState(std::string_view key, double by = 1.0) {
        auto cur = stateDouble(key).value_or(0.0);
        setStateDouble(key, cur + by);
    }

    // Convenience: elapsed time since a stored timestamp (in milliseconds)
    [[nodiscard]] std::optional<Duration> elapsedSince(std::string_view ts_key) const {
        auto ts_opt = stateDouble(ts_key);
        if (!ts_opt) return std::nullopt;
        auto stored = Timestamp(std::chrono::milliseconds(static_cast<int64_t>(*ts_opt)));
        return std::chrono::duration_cast<Duration>(now() - stored);
    }

    // Convenience: store current timestamp
    void storeNow(std::string_view key) {
        setStateDouble(key, static_cast<double>(toUnixMs(now())));
    }
};

} // namespace meridian
