#include "meridian/pipeline.hpp"
#include "meridian/metrics.hpp"
#include <stdexcept>
#include <numeric>

namespace meridian {

// ─── PipelineStage ────────────────────────────────────────────────────────────

void PipelineStage::process(const Event& event, RuntimeAPI& api) {
    proc_->onEvent(event, api);
    ++events_processed_;
}

// ─── Pipeline ─────────────────────────────────────────────────────────────────

Pipeline& Pipeline::addStage(std::unique_ptr<ProcessorBase> proc, std::string label) {
    if (!proc) throw std::invalid_argument("Pipeline::addStage: null processor");
    if (running_) throw std::logic_error("Cannot add stages to a running pipeline");
    stages_.emplace_back(std::move(proc), std::move(label));
    return *this;
}

void Pipeline::start(RuntimeAPI& api) {
    if (running_) return;
    running_ = true;
    for (auto& stage : stages_)
        stage.processor()->onStart(api);
}

void Pipeline::stop() noexcept {
    if (!running_) return;
    running_ = false;
    for (auto& stage : stages_)
        stage.processor()->onStop();
}

void Pipeline::tick(RuntimeAPI& api) {
    for (auto& stage : stages_)
        stage.processor()->onTick(api);
}

bool Pipeline::process(const Event& event, RuntimeAPI& api) {
    // Capture suppression detection: we check if any stage emits a SuppressEffect
    // by wrapping the api in a thin adapter that watches the emit() calls.
    // To avoid the overhead in the hot path, we use a simple flag approach.

    bool suppressed = false;

    for (auto& stage : stages_) {
        if (suppressed && config_.skip_suppressed) break;

        try {
            // We need to detect SuppressEffect. To do this without modifying
            // RuntimeAPI, we use a wrapper that intercepts emit().
            struct SuppressDetectAPI : RuntimeAPI {
                RuntimeAPI&  inner;
                bool&        suppressed;

                explicit SuppressDetectAPI(RuntimeAPI& i, bool& s)
                    : inner(i), suppressed(s) {}

                std::optional<double> queryMetric(std::string_view k) const override
                    { return inner.queryMetric(k); }
                std::vector<double> queryMetricHistory(std::string_view k,
                    std::chrono::seconds w) const override
                    { return inner.queryMetricHistory(k, w); }
                Timestamp now() const override { return inner.now(); }
                std::optional<std::string> getConfig(std::string_view k) const override
                    { return inner.getConfig(k); }
                std::optional<std::string> getState(std::string_view k) const override
                    { return inner.getState(k); }
                void setState(std::string_view k, std::string v) override
                    { inner.setState(k, std::move(v)); }
                void clearState(std::string_view k) override
                    { inner.clearState(k); }
                void emit(Effect effect) override {
                    if (std::holds_alternative<SuppressEffect>(effect))
                        suppressed = true;
                    inner.emit(std::move(effect));
                }
            } detect_api(api, suppressed);

            stage.process(event, detect_api);

        } catch (const std::exception& ex) {
            stage.errors_++;
            if (config_.stop_on_error) throw;
            api.emit(NotifyEffect{
                NotifyEffect::Channel::LOG, "",
                "Pipeline processor error",
                std::string(stage.label()) + ": " + ex.what(),
                "error", ""
            });
        } catch (...) {
            stage.errors_++;
            if (config_.stop_on_error) throw;
        }
    }

    MERIDIAN_COUNTER("pipeline.events_processed").increment();
    return !suppressed;
}

uint64_t Pipeline::totalEvents() const {
    uint64_t total = 0;
    for (const auto& s : stages_) total += s.eventsProcessed();
    return stages_.empty() ? 0 : stages_.front().eventsProcessed();
}

// ─── PipelineBuilder ──────────────────────────────────────────────────────────

Pipeline PipelineBuilder::build() {
    Pipeline p(cfg_);
    for (auto& proc : procs_)
        p.addStage(std::move(proc));
    procs_.clear();
    return p;
}

} // namespace meridian
