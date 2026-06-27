#pragma once

#include "processor.hpp"
#include "event_bus.hpp"
#include "metrics.hpp"
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <stdexcept>
#include <chrono>

namespace meridian {

// ─── PipelineStage: a single processor slot in the chain ─────────────────────
class PipelineStage {
public:
    explicit PipelineStage(std::unique_ptr<ProcessorBase> proc, std::string label = "")
        : proc_(std::move(proc))
        , label_(label.empty() ? std::string(proc_->name()) : std::move(label))
    {}

    // Non-copyable, movable
    PipelineStage(PipelineStage&&) = default;
    PipelineStage& operator=(PipelineStage&&) = default;
    PipelineStage(const PipelineStage&) = delete;
    PipelineStage& operator=(const PipelineStage&) = delete;

    void process(const Event& event, RuntimeAPI& api);

    [[nodiscard]] std::string_view    label()           const { return label_; }
    [[nodiscard]] uint64_t            eventsProcessed() const { return events_processed_; }
    [[nodiscard]] uint64_t            errors()          const { return errors_; }
    [[nodiscard]] ProcessorBase*      processor()             { return proc_.get(); }
    [[nodiscard]] const ProcessorBase* processor()      const { return proc_.get(); }

private:
    friend class Pipeline;
    std::unique_ptr<ProcessorBase> proc_;
    std::string                    label_;
    uint64_t                       events_processed_{0};
    uint64_t                       errors_{0};
};

// ─── PipelineConfig ───────────────────────────────────────────────────────────
struct PipelineConfig {
    std::string name          = "unnamed";
    bool        stop_on_error = false;    // halt pipeline on processor exception
    bool        skip_suppressed = true;   // stop chain when SuppressEffect emitted
};

// ─── Pipeline: executes an ordered chain of processors ───────────────────────
//
// When process(event, api) is called:
//  1. Each stage's processor calls api.emit() for any effects it produces.
//  2. If a stage emits SuppressEffect and skip_suppressed==true, the chain halts.
//  3. On exception: if stop_on_error==true, rethrow; otherwise log and continue.
//
// The same Pipeline instance runs identically in SimRuntime and ProdRuntime;
// only the RuntimeAPI implementation differs.

class Pipeline {
public:
    explicit Pipeline(PipelineConfig config = {}) : config_(std::move(config)) {}

    // Non-copyable
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) = default;
    Pipeline& operator=(Pipeline&&) = default;

    // ── Builder API ───────────────────────────────────────────────────────────
    Pipeline& addStage(std::unique_ptr<ProcessorBase> proc, std::string label = "");

    template<typename ProcessorT, typename... Args>
    Pipeline& emplace(Args&&... args) {
        return addStage(std::make_unique<ProcessorT>(std::forward<Args>(args)...));
    }

    template<typename ProcessorT, typename... Args>
    Pipeline& emplaceLabeled(std::string label, Args&&... args) {
        return addStage(std::make_unique<ProcessorT>(std::forward<Args>(args)...),
                        std::move(label));
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void start(RuntimeAPI& api);    // Calls onStart() on all processors
    void stop()  noexcept;          // Calls onStop()  on all processors
    void tick(RuntimeAPI& api);     // Calls onTick()  on all processors

    // ── Execution ─────────────────────────────────────────────────────────────
    // Returns false if event was suppressed
    bool process(const Event& event, RuntimeAPI& api);

    // ── Introspection ─────────────────────────────────────────────────────────
    [[nodiscard]] const PipelineConfig&           config()      const { return config_; }
    [[nodiscard]] size_t                          stageCount()  const { return stages_.size(); }
    [[nodiscard]] bool                            isRunning()   const { return running_; }
    [[nodiscard]] uint64_t                        totalEvents() const;
    [[nodiscard]] const std::vector<PipelineStage>& stages()   const { return stages_; }

private:
    PipelineConfig             config_;
    std::vector<PipelineStage> stages_;
    bool                       running_{false};
};

// ─── PipelineBuilder: fluent factory ──────────────────────────────────────────
class PipelineBuilder {
public:
    PipelineBuilder() = default;
    explicit PipelineBuilder(std::string name) { cfg_.name = std::move(name); }

    PipelineBuilder& stopOnError(bool stop = true) {
        cfg_.stop_on_error = stop; return *this;
    }
    PipelineBuilder& skipSuppressed(bool skip = true) {
        cfg_.skip_suppressed = skip; return *this;
    }

    template<typename ProcessorT, typename... Args>
    PipelineBuilder& then(Args&&... args) {
        procs_.push_back(std::make_unique<ProcessorT>(std::forward<Args>(args)...));
        return *this;
    }

    // Aliases for ergonomics — identical to then<T>()
    template<typename ProcessorT, typename... Args>
    PipelineBuilder& emplace(Args&&... args) {
        return then<ProcessorT>(std::forward<Args>(args)...);
    }

    template<typename ProcessorT, typename... Args>
    PipelineBuilder& emplaceLabeled(std::string /*label*/, Args&&... args) {
        // label stored on PipelineStage; PipelineBuilder::build() assigns it
        return then<ProcessorT>(std::forward<Args>(args)...);
    }

    [[nodiscard]] Pipeline build();

private:
    PipelineConfig                             cfg_;
    std::vector<std::unique_ptr<ProcessorBase>> procs_;
};

} // namespace meridian
