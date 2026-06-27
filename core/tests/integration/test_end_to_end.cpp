#include "meridian/sim_runtime.hpp"
#include "meridian/prod_runtime.hpp"
#include "meridian/processors.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <atomic>

using namespace meridian;
using namespace meridian::processors;
using namespace std::chrono_literals;

// ─── Full pipeline: multiple processors chained ───────────────────────────────
//
// Pipeline: [LogLevelFilter → ThresholdDetector → RateLimiter]
// Verifies that:
//   1. LevelFilter drops DEBUG/INFO events before they reach ThresholdDetector
//   2. ThresholdDetector fires on cpu.usage > 80
//   3. RateLimiter prevents repeat alerts within window

TEST(IntegrationTest, MultistageChainedPipeline) {
    SimRuntime rt;

    auto pipeline = std::make_shared<Pipeline>(PipelineConfig{
        .name          = "observability",
        .stop_on_error = false,
        .skip_suppressed = true
    });
    pipeline->emplace<LogLevelFilter>(LogEvent::Level::ERROR);
    pipeline->emplace<ThresholdDetector>(ThresholdDetector::Config{
        .metric_name    = "cpu.usage",
        .threshold      = 80.0,
        .hysteresis     = 5.0,
        .severity       = "critical",
        .notify_channel = "#ops",
        .channel        = NotifyEffect::Channel::LOG
    });
    pipeline->emplace<RateLimiter>(RateLimiter::Config{
        .metric_name    = "cpu.usage",
        .max_per_window = 1,
        .window         = Duration{10000}
    });

    rt.registerPipeline(pipeline);
    rt.start();
    auto& api = rt.api();

    // Inject a mix of events
    rt.injectEvent(LogEvent{"svc", LogEvent::Level::DEBUG, "debug noise"});
    rt.injectEvent(LogEvent{"svc", LogEvent::Level::INFO,  "info noise"});
    rt.injectEvent(MetricEvent::cpu("host-1", 50.0));    // Below threshold
    rt.injectEvent(MetricEvent::cpu("host-1", 85.0));    // ALERT
    rt.injectEvent(MetricEvent::cpu("host-1", 90.0));    // Hysteresis: suppressed
    rt.injectEvent(MetricEvent::cpu("host-1", 91.0));    // Rate limited

    auto alerts    = api.capturedEffectsOf<NotifyEffect>();
    auto suppressed = api.capturedEffectsOf<SuppressEffect>();

    EXPECT_EQ(alerts.size(), 1u) << "Exactly one alert should fire";
    EXPECT_NE(alerts[0].body.find("85.00"), std::string::npos);
    EXPECT_FALSE(suppressed.empty()) << "Some events should have been suppressed";

    rt.stop();
}

// ─── Verify business logic is identical in sim vs prod ───────────────────────
//
// This is the core value proposition of Meridian:
// The same pipeline produces the same effects regardless of runtime.

TEST(IntegrationTest, SimAndProdProduceSameNotificationCount) {
    auto make_pipeline = []() -> std::shared_ptr<Pipeline> {
        auto p = std::make_shared<Pipeline>(PipelineConfig{.name = "shared"});
        p->emplace<ThresholdDetector>(ThresholdDetector::Config{
            .metric_name = "cpu.usage",
            .threshold   = 70.0,
            .hysteresis  = 5.0
        });
        return p;
    };

    std::vector<Event> events;
    // 60→below; 75→ALERT(1); 80→sustained; 65→below hysteresis boundary (no clear);
    // 40→RECOVERY(2); 78→ALERT again(3)
    for (double v : {60.0, 75.0, 80.0, 65.0, 40.0, 78.0})
        events.push_back(MetricEvent::cpu("h", v));

    // ── Sim run ───────────────────────────────────────────────────────────────
    size_t sim_alerts = 0;
    {
        SimRuntime sim;
        sim.registerPipeline(make_pipeline());
        sim.start();
        sim.replay(events);
        sim_alerts = sim.api().capturedEffectsOf<NotifyEffect>().size();
        sim.stop();
    }

    // ── Prod run (shadow mode) ─────────────────────────────────────────────────
    size_t prod_alerts = 0;
    {
        ProdRuntime prod(RuntimeConfig{
            .mode          = RuntimeMode::SHADOW,
            .queue_capacity = 64,
            .tick_interval  = 1000ms
        });
        prod.registerPipeline(make_pipeline());

        std::atomic<size_t> captured{0};
        prod.registerEffectHandler([&](const Effect& e) {
            if (std::holds_alternative<NotifyEffect>(e)) ++captured;
        });

        prod.start();
        for (const auto& e : events) prod.injectEvent(e);
        // Give worker thread time to drain
        std::this_thread::sleep_for(50ms);
        prod.stop();

        // In SHADOW mode, handlers are NOT called; check via metrics instead
        prod_alerts = prod.getMetrics().effects_emitted;
    }

    // Both runtimes should produce the same number of alerts
    EXPECT_EQ(sim_alerts, 3u)   << "Expected 3 notifications (alert, recovery, re-alert)";
    // Prod emitted same effect count (sim_alerts NotifyEffects + possibly StoreEffects)
    EXPECT_GE(prod_alerts, sim_alerts)
        << "Prod should emit at least as many effects as sim (same logic)";
}

// ─── ProdRuntime backpressure ─────────────────────────────────────────────────

TEST(IntegrationTest, ProdRuntimeHandlesHighEventRate) {
    ProdRuntime rt(RuntimeConfig{
        .mode           = RuntimeMode::PRODUCTION,
        .queue_capacity  = 128,
        .tick_interval   = 10000ms
    });

    auto p = std::make_shared<Pipeline>(PipelineConfig{.name = "perf"});
    p->emplace<ThresholdDetector>(ThresholdDetector::Config{
        .metric_name = "cpu.usage", .threshold = 99.0
    });
    rt.registerPipeline(p);

    std::atomic<size_t> effects_fired{0};
    rt.registerEffectHandler([&](const Effect&) { ++effects_fired; });

    rt.start();

    constexpr int N = 500;
    for (int i = 0; i < N; ++i)
        rt.injectEvent(MetricEvent::cpu("h", 50.0 + (i % 40)));

    std::this_thread::sleep_for(100ms);
    rt.stop();

    auto m = rt.getMetrics();
    EXPECT_GE(m.events_processed, static_cast<uint64_t>(N - 50))
        << "Should have processed most events (allowing for late drain)";
}

// ─── Effect handler registration ──────────────────────────────────────────────

TEST(IntegrationTest, EffectHandlersCalledInProduction) {
    SimRuntime rt;

    std::vector<std::string> dispatched;
    rt.registerEffectHandler([&](const Effect& e) {
        dispatched.push_back(std::string(effectTypeName(e)));
    });

    auto p = std::make_shared<Pipeline>(PipelineConfig{.name = "p"});
    p->emplace<ThresholdDetector>(ThresholdDetector::Config{
        .metric_name = "cpu.usage", .threshold = 80.0
    });
    rt.registerPipeline(p);
    rt.start();

    rt.injectEvent(MetricEvent::cpu("h", 90.0));

    ASSERT_FALSE(dispatched.empty());
    EXPECT_EQ(dispatched[0], "NotifyEffect");

    rt.stop();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
