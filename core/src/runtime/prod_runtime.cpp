#include "meridian/prod_runtime.hpp"
#include "meridian/sim_runtime.hpp"
#include "meridian/metrics.hpp"
#include <algorithm>
#include <stdexcept>
#include <thread>
#include <chrono>

namespace meridian {

// ─── ProdRuntimeAPI ───────────────────────────────────────────────────────────

void ProdRuntimeAPI::ingestMetric(const std::string& key, double value, Timestamp ts) {
    std::lock_guard<std::mutex> lk(data_mutex_);
    auto& deq = history_[key];
    deq.push_back({value, ts});
    // Keep at most 1000 samples per key (rolling window)
    if (deq.size() > 1000) deq.pop_front();
}

std::optional<double> ProdRuntimeAPI::queryMetric(std::string_view key) const {
    std::lock_guard<std::mutex> lk(data_mutex_);
    auto it = history_.find(std::string(key));
    if (it == history_.end() || it->second.empty()) return std::nullopt;
    return it->second.back().value;
}

std::vector<double> ProdRuntimeAPI::queryMetricHistory(
        std::string_view key, std::chrono::seconds window) const {
    std::lock_guard<std::mutex> lk(data_mutex_);
    auto it = history_.find(std::string(key));
    if (it == history_.end()) return {};

    auto cutoff = Clock::now() - window;
    std::vector<double> out;
    for (const auto& sample : it->second)
        if (sample.ts >= cutoff)
            out.push_back(sample.value);
    return out;
}

std::optional<std::string> ProdRuntimeAPI::getConfig(std::string_view key) const {
    std::lock_guard<std::mutex> lk(data_mutex_);
    auto it = config_.find(std::string(key));
    if (it == config_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::string> ProdRuntimeAPI::getState(std::string_view key) const {
    std::lock_guard<std::mutex> lk(data_mutex_);
    auto it = state_.find(std::string(key));
    if (it == state_.end()) return std::nullopt;
    return it->second;
}

void ProdRuntimeAPI::setState(std::string_view key, std::string value) {
    std::lock_guard<std::mutex> lk(data_mutex_);
    state_[std::string(key)] = std::move(value);
}

void ProdRuntimeAPI::clearState(std::string_view key) {
    std::lock_guard<std::mutex> lk(data_mutex_);
    state_.erase(std::string(key));
}

void ProdRuntimeAPI::emit(Effect effect) {
    if (effect_dispatcher_) effect_dispatcher_(std::move(effect));
}

void ProdRuntimeAPI::setEffectDispatcher(std::function<void(Effect)> dispatcher) {
    effect_dispatcher_ = std::move(dispatcher);
}

void ProdRuntimeAPI::setConfig(std::string key, std::string value) {
    std::lock_guard<std::mutex> lk(data_mutex_);
    config_[std::move(key)] = std::move(value);
}

// ─── ProdRuntime ──────────────────────────────────────────────────────────────

ProdRuntime::ProdRuntime(RuntimeConfig config)
    : config_(std::move(config))
    , api_(std::make_unique<ProdRuntimeAPI>())
{}

ProdRuntime::~ProdRuntime() {
    if (running_.load(std::memory_order_acquire)) stop();
}

void ProdRuntime::registerPipeline(std::shared_ptr<Pipeline> pipeline) {
    if (!pipeline) throw std::invalid_argument("ProdRuntime: null pipeline");
    std::lock_guard<std::mutex> lk(queue_mutex_);
    pipelines_.push_back(std::move(pipeline));
}

void ProdRuntime::injectEvent(Event event) {
    if (!running_.load(std::memory_order_acquire))
        throw std::logic_error("ProdRuntime::injectEvent: not started");

    std::unique_lock<std::mutex> lk(queue_mutex_);
    // Apply backpressure: block until queue has space
    queue_cv_.wait(lk, [this] {
        return event_queue_.size() < config_.queue_capacity
               || !running_.load(std::memory_order_relaxed);
    });

    if (!running_.load(std::memory_order_relaxed)) return;

    // Update metric store from MetricEvent
    if (const auto* me = std::get_if<MetricEvent>(&event))
        api_->ingestMetric(me->name, me->value, me->timestamp);

    event_queue_.push(std::move(event));
    lk.unlock();
    queue_cv_.notify_one();
}

void ProdRuntime::injectBatch(std::vector<Event> events) {
    for (auto& e : events) injectEvent(std::move(e));
}

void ProdRuntime::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) return;

    // Set up effect dispatcher on the API
    bool shadow = (config_.mode == RuntimeMode::SHADOW);
    api_->setEffectDispatcher([this, shadow](Effect effect) {
        effects_emitted_.fetch_add(1, std::memory_order_relaxed);
        if (!shadow) {
            // Dispatch to registered handlers
            std::lock_guard<std::mutex> lk(handler_mutex_);
            for (auto& h : effect_handlers_) h(effect);
        }
        // In shadow mode: effect is counted but handlers are NOT called
    });

    // Start pipelines
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        for (auto& p : pipelines_) p->start(*api_);
    }

    // Dispatch worker thread
    dispatch_thread_ = std::jthread([this](std::stop_token st) {
        dispatchLoop();
    });

    // Tick thread
    tick_thread_ = std::jthread([this](std::stop_token st) {
        tickLoop();
    });

    MERIDIAN_GAUGE("runtime.mode").set(
        config_.mode == RuntimeMode::PRODUCTION ? 1 : 2);
}

void ProdRuntime::stop() {
    running_.store(false, std::memory_order_release);
    queue_cv_.notify_all();

    if (dispatch_thread_.joinable()) dispatch_thread_.join();
    if (tick_thread_.joinable())     tick_thread_.join();

    std::lock_guard<std::mutex> lk(queue_mutex_);
    for (auto& p : pipelines_) p->stop();
}

void ProdRuntime::dispatchLoop() {
    while (running_.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> lk(queue_mutex_);
        queue_cv_.wait(lk, [this] {
            return !event_queue_.empty()
                   || !running_.load(std::memory_order_relaxed);
        });

        while (!event_queue_.empty()) {
            Event event = std::move(event_queue_.front());
            event_queue_.pop();
            lk.unlock();
            queue_cv_.notify_one();  // Signal producers waiting for space

            for (auto& pipeline : pipelines_) {
                try {
                    pipeline->process(event, *api_);
                } catch (const std::exception&) {
                    processor_errors_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            events_processed_.fetch_add(1, std::memory_order_relaxed);
            MERIDIAN_COUNTER("runtime.events_processed").increment();

            lk.lock();
        }
    }

    // Drain remaining events after stop signal
    std::lock_guard<std::mutex> lk(queue_mutex_);
    while (!event_queue_.empty()) {
        auto event = std::move(event_queue_.front());
        event_queue_.pop();
        for (auto& pipeline : pipelines_) {
            try { pipeline->process(event, *api_); }
            catch (...) {}
        }
        events_dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

void ProdRuntime::tickLoop() {
    while (running_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(config_.tick_interval);
        if (!running_.load(std::memory_order_acquire)) break;
        std::lock_guard<std::mutex> lk(queue_mutex_);
        for (auto& p : pipelines_) p->tick(*api_);
    }
}

size_t ProdRuntime::queueDepth() const {
    std::lock_guard<std::mutex> lk(queue_mutex_);
    return event_queue_.size();
}

RuntimeMetrics ProdRuntime::getMetrics() const {
    return RuntimeMetrics{
        .events_processed  = events_processed_.load(std::memory_order_relaxed),
        .events_dropped    = events_dropped_.load(std::memory_order_relaxed),
        .effects_emitted   = effects_emitted_.load(std::memory_order_relaxed),
        .processor_errors  = processor_errors_.load(std::memory_order_relaxed),
        .queue_depth       = queueDepth(),
        .active_pipelines  = pipelines_.size()
    };
}

// ─── Factory ──────────────────────────────────────────────────────────────────
std::unique_ptr<Runtime> createRuntime(RuntimeConfig config) {
    if (config.mode == RuntimeMode::SIMULATION)
        return std::make_unique<SimRuntime>(config);
    return std::make_unique<ProdRuntime>(config);
}

} // namespace meridian
