#pragma once

#include "events.hpp"
#include "effects.hpp"
#include "runtime_api.hpp"
#include <concepts>
#include <string_view>
#include <memory>

namespace meridian {

// ─── Processor concept ────────────────────────────────────────────────────────
//
// A Processor is the fundamental unit of business logic in Meridian.
// It consumes events and emits effects via RuntimeAPI — never performing
// I/O directly. This purity enables:
//   • Deterministic unit testing (no mocking needed — SimRuntime suffices)
//   • Zero-change business-logic reuse across all three runtime modes
//   • Compositional pipelines (processors chain cleanly)
//
// Two ways to define a processor:
//   1. Satisfy the ProcessorConcept (duck-typed, no inheritance needed)
//   2. Inherit from ProcessorBase (virtual dispatch, runtime polymorphism)

template<typename T>
concept ProcessorConcept = requires(T& p, const Event& e, RuntimeAPI& api) {
    { p.onEvent(e, api) } -> std::same_as<void>;
    { T::name() } -> std::convertible_to<std::string_view>;
};

// ─── ProcessorBase: virtual interface for runtime polymorphism ────────────────
class ProcessorBase {
public:
    virtual ~ProcessorBase() = default;

    // Called for every event that reaches this processor
    virtual void onEvent(const Event& event, RuntimeAPI& api) = 0;

    // Human-readable name for logging, metrics, and the dashboard
    [[nodiscard]] virtual std::string_view name() const = 0;

    // Lifecycle hooks — called by the Runtime around the event loop
    virtual void onStart(RuntimeAPI& /*api*/) {}
    virtual void onStop() noexcept {}

    // Optional: handle the end of a time window (for stateful processors)
    virtual void onTick(RuntimeAPI& /*api*/) {}
};

// ─── Adapter: wrap a ProcessorConcept into a ProcessorBase ───────────────────
// Allows duck-typed structs to be used in polymorphic pipelines.
template<ProcessorConcept T>
class ProcessorAdapter : public ProcessorBase {
public:
    template<typename... Args>
    explicit ProcessorAdapter(Args&&... args)
        : impl_(std::forward<Args>(args)...) {}

    void onEvent(const Event& event, RuntimeAPI& api) override {
        impl_.onEvent(event, api);
    }

    [[nodiscard]] std::string_view name() const override {
        return T::name();
    }

    void onStart(RuntimeAPI& api) override {
        if constexpr (requires { impl_.onStart(api); }) {
            impl_.onStart(api);
        }
    }

    void onStop() noexcept override {
        if constexpr (requires { impl_.onStop(); }) {
            impl_.onStop();
        }
    }

    [[nodiscard]] T& impl() { return impl_; }

private:
    T impl_;
};

// Factory helper
template<ProcessorConcept T, typename... Args>
[[nodiscard]] std::unique_ptr<ProcessorBase> makeProcessor(Args&&... args) {
    return std::make_unique<ProcessorAdapter<T>>(std::forward<Args>(args)...);
}

} // namespace meridian
