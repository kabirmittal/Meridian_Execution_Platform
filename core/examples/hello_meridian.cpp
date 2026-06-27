// ─── hello_meridian.cpp ───────────────────────────────────────────────────────
// Minimal "getting started" example.
// Demonstrates: event injection, threshold detection, effect capture.
//
// Build:  cmake -B build && cmake --build build --target hello_meridian
// Run:    ./build/hello_meridian

#include "meridian/sim_runtime.hpp"
#include "meridian/processors.hpp"
#include <iostream>

using namespace meridian;
using namespace meridian::processors;

int main() {
    std::cout << "=== Meridian Hello World ===\n\n";

    // 1. Create a SimRuntime (deterministic, no real I/O)
    SimRuntime rt;

    // 2. Register a handler to print effects to stdout
    rt.registerEffectHandler([](const Effect& e) {
        std::visit([](const auto& eff) {
            using T = std::decay_t<decltype(eff)>;
            if constexpr (std::is_same_v<T, NotifyEffect>)
                std::cout << "[NOTIFY] " << eff.title << "\n"
                          << "        " << eff.body   << "\n\n";
            else if constexpr (std::is_same_v<T, StoreEffect>)
                std::cout << "[STORE]  key=" << eff.key << "\n\n";
        }, e);
    });

    // 3. Build a pipeline: watch cpu.usage, alert if > 80%
    auto pipeline = std::make_shared<Pipeline>(PipelineConfig{.name = "demo"});
    pipeline->emplace<ThresholdDetector>(ThresholdDetector::Config{
        .metric_name    = "cpu.usage",
        .threshold      = 80.0,
        .hysteresis     = 10.0,
        .severity       = "warning",
        .notify_channel = "#ops",
        .channel        = NotifyEffect::Channel::LOG
    });

    rt.registerPipeline(pipeline);
    rt.start();

    // 4. Inject a sequence of metric events
    std::cout << "Injecting events...\n\n";
    struct Sample { double cpu; const char* label; };
    const Sample samples[] = {
        {45.0, "normal"},
        {62.0, "elevated"},
        {81.5, "ABOVE THRESHOLD"},
        {85.0, "still high (hysteresis)"},
        {68.0, "recovering (above 80-10=70)"},
        {65.0, "RECOVERED"},
        {90.0, "spike again"}
    };

    for (const auto& [cpu, label] : samples) {
        std::cout << ">> cpu.usage=" << cpu << "% (" << label << ")\n";
        rt.injectEvent(MetricEvent::cpu("host-01", cpu));
    }

    // 5. Print summary
    std::cout << "\n=== Summary ===\n";
    auto m = rt.getMetrics();
    std::cout << "Events processed: " << m.events_processed   << "\n"
              << "Effects emitted:  " << m.effects_emitted    << "\n"
              << "Notify effects:   "
              << rt.api().capturedEffectsOf<NotifyEffect>().size() << "\n";

    rt.stop();
    return 0;
}
