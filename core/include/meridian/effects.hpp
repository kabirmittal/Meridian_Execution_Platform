#pragma once

#include "events.hpp"
#include <string>
#include <variant>
#include <vector>
#include <optional>
#include <chrono>

namespace meridian {

// ─── Effect types: output intents produced by processors ─────────────────────
// Processors never perform I/O directly — they return Effects.
// The Runtime's effect dispatcher decides how to actually execute them.
// This separation enables 100% business-logic reuse across sim/shadow/prod.

// ─── NotifyEffect: send an alert through some notification channel ────────────
struct NotifyEffect {
    enum class Channel : uint8_t { SLACK, WEBHOOK, EMAIL, PAGERDUTY, LOG };

    Channel     channel{Channel::LOG};
    std::string recipient;       // Slack channel, email addr, webhook URL, etc.
    std::string title;
    std::string body;
    std::string severity;        // "info" | "warning" | "critical"
    std::string fingerprint;     // dedup key — same fingerprint → deduplicated

    static NotifyEffect slack(std::string channel_name, std::string title,
                               std::string body, std::string sev = "info") {
        return {Channel::SLACK, std::move(channel_name),
                std::move(title), std::move(body), std::move(sev), ""};
    }
    static NotifyEffect webhook(std::string url, std::string title,
                                 std::string body, std::string sev = "info") {
        return {Channel::WEBHOOK, std::move(url),
                std::move(title), std::move(body), std::move(sev), ""};
    }
    static NotifyEffect log(std::string title, std::string body,
                             std::string sev = "info") {
        return {Channel::LOG, "", std::move(title), std::move(body), std::move(sev), ""};
    }
};

// ─── StoreEffect: persist data to a backend ──────────────────────────────────
struct StoreEffect {
    enum class Backend : uint8_t { TIMESERIES, KEY_VALUE, EVENT_LOG };

    Backend     backend{Backend::EVENT_LOG};
    std::string key;
    std::string value;       // JSON-encoded
    std::optional<Duration> ttl;

    static StoreEffect timeseries(std::string metric, double value) {
        return {Backend::TIMESERIES, std::move(metric),
                std::to_string(value), std::nullopt};
    }
    static StoreEffect kv(std::string key, std::string value,
                           std::optional<Duration> ttl = std::nullopt) {
        return {Backend::KEY_VALUE, std::move(key), std::move(value), ttl};
    }
};

// ─── ForwardEffect: re-publish (transformed) event to another topic ───────────
struct ForwardEffect {
    std::string topic;
    std::string payload;       // JSON-encoded event or derived payload
    int         priority{0};   // higher = processed first
};

// ─── SuppressEffect: drop the current event, don't process further ────────────
struct SuppressEffect {
    std::string reason;
    Duration    suppress_for{0};  // 0 = suppress once; >0 = suppress window
};

// ─── NoEffect: processor ran but produced no output ──────────────────────────
struct NoEffect {};

// ─── The discriminated union over all effect types ───────────────────────────
using Effect  = std::variant<NotifyEffect, StoreEffect, ForwardEffect,
                              SuppressEffect, NoEffect>;
using Effects = std::vector<Effect>;

// ─── Effect utilities ─────────────────────────────────────────────────────────
[[nodiscard]] inline std::string_view effectTypeName(const Effect& e) {
    return std::visit([]<typename T>(const T&) -> std::string_view {
        if constexpr (std::is_same_v<T, NotifyEffect>)   return "NotifyEffect";
        if constexpr (std::is_same_v<T, StoreEffect>)    return "StoreEffect";
        if constexpr (std::is_same_v<T, ForwardEffect>)  return "ForwardEffect";
        if constexpr (std::is_same_v<T, SuppressEffect>) return "SuppressEffect";
        if constexpr (std::is_same_v<T, NoEffect>)       return "NoEffect";
    }, e);
}

[[nodiscard]] inline bool isSuppression(const Effect& e) {
    return std::holds_alternative<SuppressEffect>(e);
}

// ─── Convenience builders ─────────────────────────────────────────────────────
inline Effects effects_of(auto&&... args) {
    return Effects{std::forward<decltype(args)>(args)...};
}

inline Effects no_effects() { return {}; }

} // namespace meridian
