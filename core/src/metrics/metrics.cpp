#include "meridian/metrics.hpp"
#include <stdexcept>

namespace meridian {

MetricsRegistry& MetricsRegistry::instance() {
    static MetricsRegistry inst;
    return inst;
}

Counter& MetricsRegistry::counter(const std::string& name) {
    std::lock_guard<std::mutex> lk(mutex_);
    return counters_[name];   // Default-constructs if absent
}

Gauge& MetricsRegistry::gauge(const std::string& name) {
    std::lock_guard<std::mutex> lk(mutex_);
    return gauges_[name];
}

std::unordered_map<std::string, uint64_t> MetricsRegistry::snapshotCounters() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::unordered_map<std::string, uint64_t> out;
    out.reserve(counters_.size());
    for (const auto& [k, v] : counters_)
        out.emplace(k, v.load());
    return out;
}

std::unordered_map<std::string, int64_t> MetricsRegistry::snapshotGauges() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::unordered_map<std::string, int64_t> out;
    out.reserve(gauges_.size());
    for (const auto& [k, v] : gauges_)
        out.emplace(k, v.load());
    return out;
}

void MetricsRegistry::reset() {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& [k, v] : counters_) v.reset();
    for (auto& [k, v] : gauges_)   v.reset();
}

} // namespace meridian
