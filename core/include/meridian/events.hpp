#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <variant>
#include <cstdint>
#include <unordered_map>
#include <optional>
#include <string_view>
#include <algorithm>

namespace meridian {

// ─── Time utilities ──────────────────────────────────────────────────────────
using Clock     = std::chrono::system_clock;
using Timestamp = Clock::time_point;
using Duration  = std::chrono::milliseconds;

inline Timestamp now() { return Clock::now(); }

inline int64_t toUnixMs(Timestamp ts) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        ts.time_since_epoch()).count();
}

// ─── MetricEvent: a numeric measurement from any source ──────────────────────
// Examples: cpu.usage=87.3, memory.rss=1073741824, http.latency_p99=142
struct MetricEvent {
    std::string source;    // e.g. "host-42", "api-gateway", "db-primary"
    std::string name;      // e.g. "cpu.usage", "http.req.count"
    double      value;
    std::string unit;      // "percent", "bytes", "ms", "count"
    Timestamp   timestamp{now()};

    // Convenience factories
    static MetricEvent cpu(std::string src, double pct) {
        return {std::move(src), "cpu.usage", pct, "percent"};
    }
    static MetricEvent memory(std::string src, int64_t bytes) {
        return {std::move(src), "memory.rss", static_cast<double>(bytes), "bytes"};
    }
    static MetricEvent latency(std::string src, double ms) {
        return {std::move(src), "http.latency_p99", ms, "ms"};
    }
    static MetricEvent counter(std::string src, std::string metric, double count) {
        return {std::move(src), std::move(metric), count, "count"};
    }
};

// ─── LogEvent: a structured log entry ────────────────────────────────────────
struct LogEvent {
    enum class Level : uint8_t { DEBUG = 0, INFO, WARN, ERROR, CRITICAL };

    std::string source;
    Level       level{Level::INFO};
    std::string message;
    std::unordered_map<std::string, std::string> tags;
    Timestamp   timestamp{now()};

    [[nodiscard]] static std::string_view levelName(Level l) {
        switch (l) {
            case Level::DEBUG:    return "DEBUG";
            case Level::INFO:     return "INFO";
            case Level::WARN:     return "WARN";
            case Level::ERROR:    return "ERROR";
            case Level::CRITICAL: return "CRITICAL";
        }
        return "UNKNOWN";
    }
    [[nodiscard]] std::string_view levelName() const { return levelName(level); }
    [[nodiscard]] bool isError() const {
        return level == Level::ERROR || level == Level::CRITICAL;
    }
};

// ─── HeartbeatEvent: periodic liveness signal from a service ─────────────────
struct HeartbeatEvent {
    std::string service;
    std::string host;
    bool        healthy{true};
    std::string version;
    int64_t     uptime_seconds{0};
    Timestamp   timestamp{now()};
};

// ─── ControlEvent: operator commands injected into the pipeline ───────────────
// Commands: "pause", "resume", "reload_config", "drain", "shutdown"
struct ControlEvent {
    std::string command;
    std::string payload;      // JSON-encoded arguments
    std::string request_id;
    std::string issued_by;    // operator name / system
    Timestamp   timestamp{now()};
};

// ─── The discriminated union over all event types ────────────────────────────
using Event = std::variant<MetricEvent, LogEvent, HeartbeatEvent, ControlEvent>;

// ─── Event utilities ─────────────────────────────────────────────────────────
[[nodiscard]] inline Timestamp eventTimestamp(const Event& e) {
    return std::visit([](const auto& ev) { return ev.timestamp; }, e);
}

[[nodiscard]] inline std::string eventSource(const Event& e) {
    return std::visit([](const auto& ev) -> std::string {
        using T = std::decay_t<decltype(ev)>;
        if constexpr (std::is_same_v<T, ControlEvent>)
            return ev.issued_by.empty() ? "control" : ev.issued_by;
        else if constexpr (std::is_same_v<T, HeartbeatEvent>)
            return ev.service;
        else
            return ev.source;
    }, e);
}

[[nodiscard]] inline std::string_view eventTypeName(const Event& e) {
    return std::visit([]<typename T>(const T&) -> std::string_view {
        if constexpr (std::is_same_v<T, MetricEvent>)    return "MetricEvent";
        if constexpr (std::is_same_v<T, LogEvent>)       return "LogEvent";
        if constexpr (std::is_same_v<T, HeartbeatEvent>) return "HeartbeatEvent";
        if constexpr (std::is_same_v<T, ControlEvent>)   return "ControlEvent";
    }, e);
}

[[nodiscard]] inline bool isControl(const Event& e) {
    return std::holds_alternative<ControlEvent>(e);
}

} // namespace meridian
