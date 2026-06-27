#include "meridian/sim_runtime.hpp"
#include "meridian/metrics.hpp"
#include <algorithm>
#include <stdexcept>

namespace meridian {

// ─── SimRuntimeAPI ────────────────────────────────────────────────────────────

void SimRuntimeAPI::seedMetric(const std::string& key, double value) {
    metrics_[key] = value;
}

void SimRuntimeAPI::seedMetricHistory(const std::string& key, std::vector<double> values) {
    history_[key] = std::move(values);
    if (!history_[key].empty())
        metrics_[key] = history_[key].back();
}

void SimRuntimeAPI::appendMetricHistory(const std::string& key, double value) {
    history_[key].push_back(value);
    metrics_[key] = value;
}

void SimRuntimeAPI::seedConfig(const std::string& key, std::string value) {
    config_[key] = std::move(value);
}

std::optional<double> SimRuntimeAPI::queryMetric(std::string_view key) const {
    auto it = metrics_.find(std::string(key));
    if (it == metrics_.end()) return std::nullopt;
    return it->second;
}

std::vector<double> SimRuntimeAPI::queryMetricHistory(
        std::string_view key, std::chrono::seconds /*window*/) const {
    auto it = history_.find(std::string(key));
    if (it == history_.end()) return {};
    return it->second;
}

std::optional<std::string> SimRuntimeAPI::getConfig(std::string_view key) const {
    auto it = config_.find(std::string(key));
    if (it == config_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::string> SimRuntimeAPI::getState(std::string_view key) const {
    auto it = state_.find(std::string(key));
    if (it == state_.end()) return std::nullopt;
    return it->second;
}

void SimRuntimeAPI::setState(std::string_view key, std::string value) {
    state_[std::string(key)] = std::move(value);
}

void SimRuntimeAPI::clearState(std::string_view key) {
    state_.erase(std::string(key));
}

void SimRuntimeAPI::emit(Effect effect) {
    effects_.push_back(std::move(effect));
}

// ─── SimRuntime ───────────────────────────────────────────────────────────────

void SimRuntime::registerPipeline(std::shared_ptr<Pipeline> pipeline) {
    if (!pipeline) throw std::invalid_argument("SimRuntime: null pipeline");
    // SimRuntime is single-threaded; registration is safe at any point.
    bool was_running = running_;
    if (was_running) running_ = false;  // Temporarily pause to call start on new pipeline
    if (was_running) pipeline->start(*api_);
    pipelines_.push_back(std::move(pipeline));
    if (was_running) running_ = true;
}

void SimRuntime::start() {
    if (running_) return;
    running_ = true;
    for (auto& p : pipelines_)
        p->start(*api_);
    MERIDIAN_GAUGE("runtime.mode").set(0);  // 0 = SIMULATION
    MERIDIAN_GAUGE("runtime.pipelines").set(
        static_cast<int64_t>(pipelines_.size()));
}

void SimRuntime::stop() {
    if (!running_) return;
    running_ = false;
    for (auto& p : pipelines_)
        p->stop();
}

void SimRuntime::injectEvent(Event event) {
    if (!running_)
        throw std::logic_error("SimRuntime::injectEvent: not started");

    // Update internal metric store from MetricEvent
    if (const auto* me = std::get_if<MetricEvent>(&event)) {
        api_->seedMetric(me->name, me->value);
        auto hist = api_->queryMetricHistory(me->name, std::chrono::seconds(3600));
        hist.push_back(me->value);
        api_->seedMetricHistory(me->name, std::move(hist));
    }

    size_t captured_before = api_->effectCount();

    for (auto& pipeline : pipelines_) {
        try {
            pipeline->process(event, *api_);
        } catch (const std::exception& ex) {
            ++processor_errors_;
            // In sim mode, we re-throw if stop_on_error; otherwise swallow
            if (pipeline->config().stop_on_error) throw;
        }
    }

    size_t new_effects = api_->effectCount() - captured_before;

    ++events_processed_;
    effects_emitted_ += new_effects;

    // Dispatch newly captured effects to registered handlers
    auto all = api_->capturedEffects();
    if (all.size() > captured_before) {
        std::vector<Effect> new_ones(all.begin() + static_cast<long>(captured_before),
                                     all.end());
        dispatchEffects(new_ones);
    }

    MERIDIAN_COUNTER("runtime.events_processed").increment();
}

void SimRuntime::injectBatch(std::vector<Event> events) {
    for (auto& e : events)
        injectEvent(std::move(e));
}

void SimRuntime::replay(const std::vector<Event>& events) {
    for (const auto& e : events)
        injectEvent(e);
}

void SimRuntime::tick() {
    for (auto& p : pipelines_)
        p->tick(*api_);
}

RuntimeMetrics SimRuntime::getMetrics() const {
    return RuntimeMetrics{
        .events_processed  = events_processed_,
        .events_dropped    = 0,
        .effects_emitted   = effects_emitted_,
        .notifications_sent = static_cast<uint64_t>(
            api_->capturedEffectsOf<NotifyEffect>().size()),
        .data_stored       = static_cast<uint64_t>(
            api_->capturedEffectsOf<StoreEffect>().size()),
        .events_suppressed = events_suppressed_,
        .processor_errors  = processor_errors_,
        .events_per_second = 0.0,
        .avg_latency_us    = 0.0,
        .queue_depth       = 0,
        .active_pipelines  = pipelines_.size()
    };
}

void SimRuntime::dispatchEffects(const std::vector<Effect>& effects) {
    for (const auto& effect : effects)
        for (auto& handler : effect_handlers_)
            handler(effect);
}

} // namespace meridian
