#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <optional>

namespace meridian {

// ─── Counter: monotonically increasing, lock-free ────────────────────────────
class Counter {
public:
    void increment(uint64_t n = 1) noexcept {
        value_.fetch_add(n, std::memory_order_relaxed);
    }
    void reset() noexcept { value_.store(0, std::memory_order_relaxed); }
    [[nodiscard]] uint64_t load() const noexcept {
        return value_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> value_{0};
};

// ─── Gauge: can go up or down, lock-free ──────────────────────────────────────
class Gauge {
public:
    void set(int64_t v) noexcept { value_.store(v, std::memory_order_relaxed); }
    void inc(int64_t n = 1) noexcept { value_.fetch_add(n, std::memory_order_relaxed); }
    void dec(int64_t n = 1) noexcept { value_.fetch_sub(n, std::memory_order_relaxed); }
    void reset() noexcept { value_.store(0, std::memory_order_relaxed); }
    [[nodiscard]] int64_t load() const noexcept {
        return value_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<int64_t> value_{0};
};

// ─── EMA: exponential moving average for rate/latency tracking ───────────────
// Not thread-safe by itself; use within a single context or with external lock.
class EMA {
public:
    explicit EMA(double alpha = 0.1) : alpha_(alpha), value_(0.0), initialized_(false) {}

    void update(double sample) noexcept {
        if (!initialized_) { value_ = sample; initialized_ = true; }
        else                { value_ = alpha_ * sample + (1.0 - alpha_) * value_; }
    }
    [[nodiscard]] double value() const noexcept { return value_; }
    void reset() noexcept { initialized_ = false; value_ = 0.0; }

private:
    double alpha_;
    double value_;
    bool   initialized_;
};

// ─── RuntimeMetrics: point-in-time snapshot for dashboard/Prometheus ──────────
struct RuntimeMetrics {
    uint64_t events_processed{0};
    uint64_t events_dropped{0};
    uint64_t effects_emitted{0};
    uint64_t notifications_sent{0};
    uint64_t data_stored{0};
    uint64_t events_suppressed{0};
    uint64_t processor_errors{0};
    double   events_per_second{0.0};
    double   avg_latency_us{0.0};   // microseconds per event
    size_t   queue_depth{0};
    size_t   active_pipelines{0};
};

// ─── MetricsRegistry: central, singleton metrics store ───────────────────────
// Counters and gauges are created on first access (lazy, thread-safe).
class MetricsRegistry {
public:
    static MetricsRegistry& instance();

    [[nodiscard]] Counter& counter(const std::string& name);
    [[nodiscard]] Gauge&   gauge(const std::string& name);

    // Snapshot all values (for Prometheus scraping / dashboard polling)
    [[nodiscard]] std::unordered_map<std::string, uint64_t> snapshotCounters() const;
    [[nodiscard]] std::unordered_map<std::string, int64_t>  snapshotGauges()   const;

    // Reset all metrics (useful between test runs)
    void reset();

private:
    MetricsRegistry() = default;
    MetricsRegistry(const MetricsRegistry&) = delete;
    MetricsRegistry& operator=(const MetricsRegistry&) = delete;

    mutable std::mutex                         mutex_;
    std::unordered_map<std::string, Counter>   counters_;
    std::unordered_map<std::string, Gauge>     gauges_;
};

// ─── Macro helpers (optional, for ergonomics) ─────────────────────────────────
#define MERIDIAN_COUNTER(name) (::meridian::MetricsRegistry::instance().counter(name))
#define MERIDIAN_GAUGE(name)   (::meridian::MetricsRegistry::instance().gauge(name))

} // namespace meridian
