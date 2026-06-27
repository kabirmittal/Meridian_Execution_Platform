#pragma once

#include "processor.hpp"
#include "events.hpp"
#include "effects.hpp"
#include "runtime_api.hpp"
#include <string>
#include <string_view>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <vector>
#include <sstream>
#include <iomanip>

namespace meridian::processors {

// ─── ThresholdDetector ────────────────────────────────────────────────────────
// Fires a NotifyEffect when a MetricEvent's value crosses a threshold.
// Tracks the last-notified value to implement hysteresis (avoids alert storms).
struct ThresholdDetector : ProcessorBase {
    struct Config {
        std::string metric_name;
        double      threshold{0.0};
        double      hysteresis{0.0};        // Re-alert after value recovers this far
        std::string severity{"warning"};
        std::string notify_channel{"#alerts"};
        NotifyEffect::Channel channel{NotifyEffect::Channel::LOG};
    };

    // Full Config constructor
    explicit ThresholdDetector(Config cfg) : cfg_(std::move(cfg)) {}

    // Convenience constructor: ThresholdDetector("cpu.usage", 80.0, 10.0)
    ThresholdDetector(std::string metric, double threshold, double hysteresis = 0.0)
        : cfg_{std::move(metric), threshold, hysteresis} {}

    [[nodiscard]] std::string_view name() const override { return "ThresholdDetector"; }

    void onEvent(const Event& event, RuntimeAPI& api) override {
        const auto* me = std::get_if<MetricEvent>(&event);
        if (!me || me->name != cfg_.metric_name) return;

        auto was_fired = api.getState("fired").value_or("0") == "1";

        bool should_fire  = me->value > cfg_.threshold && !was_fired;
        bool should_clear = me->value < (cfg_.threshold - cfg_.hysteresis) && was_fired;

        if (should_fire) {
            std::ostringstream body;
            body << std::fixed << std::setprecision(2)
                 << "[" << me->source << "] " << cfg_.metric_name
                 << " = " << me->value << " " << me->unit
                 << " (threshold: " << cfg_.threshold << ")";

            api.emit(NotifyEffect{
                cfg_.channel, cfg_.notify_channel,
                "🔴 Alert: " + cfg_.metric_name + " threshold exceeded",
                body.str(), cfg_.severity, ""
            });
            api.setState("fired", "1");
            api.setStateDouble("last_value", me->value);
            api.storeNow("fired_at");

        } else if (should_clear) {
            std::ostringstream body;
            body << std::fixed << std::setprecision(2)
                 << "[" << me->source << "] " << cfg_.metric_name
                 << " = " << me->value << " " << me->unit << " (recovered)";

            api.emit(NotifyEffect{
                cfg_.channel, cfg_.notify_channel,
                "✅ Resolved: " + cfg_.metric_name,
                body.str(), "info", ""
            });
            api.clearState("fired");
            api.storeNow("resolved_at");
        }
        api.setStateDouble("last_value", me->value);
    }

private:
    Config cfg_;
};

// ─── AnomalyDetector ──────────────────────────────────────────────────────────
// Uses a rolling Z-score over a sliding window to detect statistical outliers.
// Emits NotifyEffect when |Z| exceeds the configured threshold.
struct AnomalyDetector : ProcessorBase {
    struct Config {
        std::string metric_name;
        size_t      window_size{20};        // Number of samples in the rolling window
        double      z_threshold{2.5};       // Alert when |Z| > this
        double      min_samples{5.0};       // Don't fire until we have enough data
        std::string severity{"warning"};
        NotifyEffect::Channel channel{NotifyEffect::Channel::LOG};
        std::string notify_channel{"#anomalies"};
    };

    // Full Config constructor
    explicit AnomalyDetector(Config cfg) : cfg_(std::move(cfg)) {}

    // Convenience: AnomalyDetector("cpu.usage", 20, 3.0)
    AnomalyDetector(std::string metric, size_t window, double z_thresh)
        : cfg_{std::move(metric), window, z_thresh} {}

    [[nodiscard]] std::string_view name() const override { return "AnomalyDetector"; }

    void onEvent(const Event& event, RuntimeAPI& api) override {
        const auto* me = std::get_if<MetricEvent>(&event);
        if (!me || me->name != cfg_.metric_name) return;

        // Use sliding window from RuntimeAPI history
        auto window = api.queryMetricHistory(cfg_.metric_name,
                                              std::chrono::seconds(3600));
        if (window.size() < static_cast<size_t>(cfg_.min_samples)) return;

        // Trim to window_size
        if (window.size() > cfg_.window_size) {
            window.erase(window.begin(),
                         window.begin() + static_cast<long>(window.size() - cfg_.window_size));
        }

        double mean = std::reduce(window.begin(), window.end(), 0.0) /
                      static_cast<double>(window.size());
        double var  = std::transform_reduce(window.begin(), window.end(),
                                             0.0, std::plus<double>{},
                                             [mean](double x) { return (x - mean) * (x - mean); });
        var /= static_cast<double>(window.size());
        double stddev = std::sqrt(var);

        if (stddev < 1e-9) return;  // No variance, can't compute Z-score

        double z = (me->value - mean) / stddev;

        if (std::abs(z) > cfg_.z_threshold) {
            std::ostringstream body;
            body << std::fixed << std::setprecision(2)
                 << "[" << me->source << "] " << cfg_.metric_name
                 << " Z-score=" << z
                 << " value=" << me->value
                 << " mean=" << mean << " ±" << stddev;

            api.emit(NotifyEffect{
                cfg_.channel, cfg_.notify_channel,
                "📊 Anomaly: " + cfg_.metric_name,
                body.str(), cfg_.severity, ""
            });
            api.incrementState("anomaly_count");
        }
    }

private:
    Config cfg_;
};

// ─── RateLimiter ──────────────────────────────────────────────────────────────
// Suppresses duplicate events within a configurable time window.
// Prevents alert storms: if the same alert fires 100× in 5 minutes,
// only the first one passes through; the rest get a SuppressEffect.
struct RateLimiter : ProcessorBase {
    struct Config {
        std::string metric_name;           // Empty string = apply to all events
        size_t      max_per_window{1};     // Max events to let through
        Duration    window{60'000};        // 60 seconds default
    };

    explicit RateLimiter(Config cfg) : cfg_(std::move(cfg)) {}

    // Convenience: RateLimiter("cpu.alert", 3, 60.0)  — window in seconds
    RateLimiter(std::string metric, size_t max_per_window, double window_seconds)
        : cfg_{std::move(metric), max_per_window,
               std::chrono::duration_cast<Duration>(
                   std::chrono::duration<double, std::milli>(window_seconds * 1000.0))} {}

    [[nodiscard]] std::string_view name() const override { return "RateLimiter"; }

    void onEvent(const Event& event, RuntimeAPI& api) override {
        if (!cfg_.metric_name.empty()) {
            const auto* me = std::get_if<MetricEvent>(&event);
            if (!me || me->name != cfg_.metric_name) return;
        }

        auto now_ms = static_cast<double>(toUnixMs(api.now()));
        auto window_start_opt = api.stateDouble("window_start");
        auto count_opt        = api.stateDouble("count");

        // Persist window_start on first entry
        if (!window_start_opt) {
            api.setStateDouble("window_start", now_ms);
            api.setStateDouble("count", 1.0);
            return;  // First event always allowed
        }

        double window_start = *window_start_opt;
        double count        = count_opt.value_or(0.0);

        bool window_expired = (now_ms - window_start) >
                              static_cast<double>(cfg_.window.count());

        if (window_expired) {
            api.setStateDouble("window_start", now_ms);
            api.setStateDouble("count", 1.0);
            return;  // New window → let through
        }

        if (static_cast<size_t>(count) >= cfg_.max_per_window) {
            api.emit(SuppressEffect{"rate_limit: " + cfg_.metric_name, cfg_.window});
            api.incrementState("suppressed_count");
        } else {
            api.setStateDouble("count", count + 1.0);
        }
    }

private:
    Config cfg_;
};

// ─── MetricAggregator ─────────────────────────────────────────────────────────
// Periodically stores summary stats (min/max/avg/p99) for a metric.
struct MetricAggregator : ProcessorBase {
    struct Config {
        std::string metric_name;
        size_t      flush_every_n{10};   // Emit StoreEffect every N events
    };

    explicit MetricAggregator(Config cfg) : cfg_(std::move(cfg)) {}

    // Convenience: MetricAggregator("mem.used_pct", 10)
    MetricAggregator(std::string metric, size_t flush_n)
        : cfg_{std::move(metric), flush_n} {}

    [[nodiscard]] std::string_view name() const override { return "MetricAggregator"; }

    void onEvent(const Event& event, RuntimeAPI& api) override {
        const auto* me = std::get_if<MetricEvent>(&event);
        if (!me || me->name != cfg_.metric_name) return;

        auto existing = api.getState("buffer").value_or("");
        if (!existing.empty()) existing += ",";
        existing += std::to_string(me->value);
        api.setState("buffer", existing);
        api.incrementState("count");

        auto count = static_cast<size_t>(api.stateDouble("count").value_or(0));
        if (count >= cfg_.flush_every_n) {
            std::vector<double> vals;
            std::istringstream ss(existing);
            std::string token;
            while (std::getline(ss, token, ','))
                if (!token.empty()) vals.push_back(std::stod(token));

            if (!vals.empty()) {
                std::sort(vals.begin(), vals.end());
                double mn  = vals.front();
                double mx  = vals.back();
                double avg = std::reduce(vals.begin(), vals.end()) /
                             static_cast<double>(vals.size());
                double p99 = vals[static_cast<size_t>(0.99 * vals.size())];

                std::ostringstream payload;
                payload << std::fixed << std::setprecision(4)
                        << "{\"min\":" << mn << ",\"max\":" << mx
                        << ",\"avg\":" << avg << ",\"p99\":" << p99
                        << ",\"n\":" << vals.size() << "}";

                api.emit(StoreEffect{
                    StoreEffect::Backend::TIMESERIES,
                    cfg_.metric_name + ".stats",
                    payload.str(),
                    std::nullopt
                });
            }
            api.setState("buffer", "");
            api.setStateDouble("count", 0.0);
        }
    }

private:
    Config cfg_;
};

// ─── LogLevelFilter ───────────────────────────────────────────────────────────
// Passes only LogEvents at or above a minimum level; suppresses the rest.
struct LogLevelFilter : ProcessorBase {
    explicit LogLevelFilter(LogEvent::Level min_level) : min_(min_level) {}

    [[nodiscard]] std::string_view name() const override { return "LogLevelFilter"; }

    void onEvent(const Event& event, RuntimeAPI& api) override {
        const auto* le = std::get_if<LogEvent>(&event);
        if (!le) return;
        if (le->level < min_) {
            api.emit(SuppressEffect{"log level below minimum", Duration{0}});
        }
    }

private:
    LogEvent::Level min_;
};

// ─── HeartbeatMonitor ─────────────────────────────────────────────────────────
// Detects when services miss their expected heartbeat window.
struct HeartbeatMonitor : ProcessorBase {
    struct Config {
        std::string         service;
        Duration            max_silence{30'000};
        NotifyEffect::Channel channel{NotifyEffect::Channel::LOG};
        std::string         notify_to{"#alerts"};
    };

    explicit HeartbeatMonitor(Config cfg) : cfg_(std::move(cfg)) {}

    // Convenience: HeartbeatMonitor("my-service", 30.0)  — silence in seconds
    HeartbeatMonitor(std::string service, double max_silence_seconds)
        : cfg_{std::move(service),
               std::chrono::duration_cast<Duration>(
                   std::chrono::duration<double, std::milli>(max_silence_seconds * 1000.0))} {}

    [[nodiscard]] std::string_view name() const override { return "HeartbeatMonitor"; }

    void onEvent(const Event& event, RuntimeAPI& api) override {
        const auto* hb = std::get_if<HeartbeatEvent>(&event);
        if (!hb) return;
        if (!cfg_.service.empty() && hb->service != cfg_.service) return;

        std::string key = "last_beat_" + hb->service;
        api.storeNow(key);

        if (!hb->healthy) {
            api.emit(NotifyEffect{
                cfg_.channel, cfg_.notify_to,
                "⚠️ Unhealthy: " + hb->service,
                "Service " + hb->service + " on " + hb->host + " reported unhealthy",
                "critical", ""
            });
        }
    }

    void onTick(RuntimeAPI& api) override {
        auto elapsed = api.elapsedSince("last_beat_" + cfg_.service);
        if (elapsed && *elapsed > cfg_.max_silence) {
            api.emit(NotifyEffect{
                cfg_.channel, cfg_.notify_to,
                "💀 Dead: " + cfg_.service,
                "No heartbeat from " + cfg_.service + " for " +
                std::to_string(elapsed->count()) + "ms",
                "critical", ""
            });
            api.storeNow("last_beat_" + cfg_.service);
        }
    }

private:
    Config cfg_;
};

} // namespace meridian::processors
