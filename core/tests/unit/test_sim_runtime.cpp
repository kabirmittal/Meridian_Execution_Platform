#include "meridian/sim_runtime.hpp"
#include "meridian/processors.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <vector>

using namespace meridian;
using namespace meridian::processors;

// ─── Helper: create a minimal Pipeline with one processor ─────────────────────
template<typename ProcessorT, typename... Args>
std::shared_ptr<Pipeline> makePipeline(std::string name, Args&&... args) {
    auto p = std::make_shared<Pipeline>(PipelineConfig{.name = std::move(name)});
    p->emplace<ProcessorT>(std::forward<Args>(args)...);
    return p;
}

// ─── SimRuntime basics ────────────────────────────────────────────────────────

class SimRuntimeTest : public ::testing::Test {
protected:
    SimRuntime rt;

    void SetUp() override {
        rt.start();
    }
    void TearDown() override {
        rt.stop();
    }
};

TEST_F(SimRuntimeTest, StartsAndStopsCleanly) {
    EXPECT_TRUE(rt.isRunning());
    rt.stop();
    EXPECT_FALSE(rt.isRunning());
    rt.start();  // Can restart
    EXPECT_TRUE(rt.isRunning());
}

TEST_F(SimRuntimeTest, InjectEventThrowsWhenNotStarted) {
    SimRuntime stopped;  // not started
    EXPECT_THROW(stopped.injectEvent(MetricEvent::cpu("h", 1.0)),
                 std::logic_error);
}

TEST_F(SimRuntimeTest, MetricsReflectProcessedEvents) {
    rt.registerPipeline(makePipeline<ThresholdDetector>(
        "pipe",
        ThresholdDetector::Config{"cpu.usage", 80.0}));

    rt.injectEvent(MetricEvent::cpu("host", 50.0));
    rt.injectEvent(MetricEvent::cpu("host", 60.0));
    rt.injectEvent(MetricEvent::cpu("host", 90.0));

    auto m = rt.getMetrics();
    EXPECT_EQ(m.events_processed, 3u);
}

// ─── ThresholdDetector ────────────────────────────────────────────────────────

class ThresholdDetectorTest : public ::testing::Test {
protected:
    SimRuntime rt;

    void SetUp() override {
        rt.registerPipeline(makePipeline<ThresholdDetector>(
            "threshold",
            ThresholdDetector::Config{
                .metric_name    = "cpu.usage",
                .threshold      = 80.0,
                .hysteresis     = 10.0,
                .severity       = "critical",
                .notify_channel = "#ops",
                .channel        = NotifyEffect::Channel::LOG
            }));
        rt.start();
    }
    void TearDown() override { rt.stop(); }

    auto& api() { return rt.api(); }
};

TEST_F(ThresholdDetectorTest, NoAlertBelowThreshold) {
    rt.injectEvent(MetricEvent::cpu("h", 70.0));
    rt.injectEvent(MetricEvent::cpu("h", 79.9));
    EXPECT_TRUE(api().capturedEffectsOf<NotifyEffect>().empty());
}

TEST_F(ThresholdDetectorTest, AlertFiredAboveThreshold) {
    rt.injectEvent(MetricEvent::cpu("h", 85.0));
    auto alerts = api().capturedEffectsOf<NotifyEffect>();
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].severity, "critical");
    EXPECT_NE(alerts[0].body.find("cpu.usage"), std::string::npos);
    EXPECT_NE(alerts[0].body.find("85.00"), std::string::npos);
}

TEST_F(ThresholdDetectorTest, OnlyOneAlertForSustainedBreach) {
    // Three consecutive events above threshold should produce only ONE alert
    rt.injectEvent(MetricEvent::cpu("h", 85.0));
    rt.injectEvent(MetricEvent::cpu("h", 90.0));
    rt.injectEvent(MetricEvent::cpu("h", 95.0));
    EXPECT_EQ(api().capturedEffectsOf<NotifyEffect>().size(), 1u);
}

TEST_F(ThresholdDetectorTest, RecoveryAlertAfterHysteresis) {
    rt.injectEvent(MetricEvent::cpu("h", 85.0));  // Alert fired
    rt.injectEvent(MetricEvent::cpu("h", 65.0));  // 80 - 10 = 70 → recovered

    auto alerts = api().capturedEffectsOf<NotifyEffect>();
    ASSERT_EQ(alerts.size(), 2u);
    EXPECT_NE(alerts[1].title.find("Resolved"), std::string::npos);
}

TEST_F(ThresholdDetectorTest, NoRecoveryWithoutHysteresis) {
    rt.injectEvent(MetricEvent::cpu("h", 85.0));  // Alert
    rt.injectEvent(MetricEvent::cpu("h", 75.0));  // Above (80 - 10 = 70), no recovery
    EXPECT_EQ(api().capturedEffectsOf<NotifyEffect>().size(), 1u);
}

TEST_F(ThresholdDetectorTest, IgnoresOtherMetrics) {
    rt.injectEvent(MetricEvent::memory("h", 9999999999LL));
    rt.injectEvent(MetricEvent::latency("h", 500.0));
    EXPECT_TRUE(api().capturedEffectsOf<NotifyEffect>().empty());
}

// ─── AnomalyDetector ─────────────────────────────────────────────────────────

class AnomalyDetectorTest : public ::testing::Test {
protected:
    SimRuntime rt;

    void SetUp() override {
        rt.registerPipeline(makePipeline<AnomalyDetector>(
            "anomaly",
            AnomalyDetector::Config{
                .metric_name   = "cpu.usage",
                .window_size   = 20,
                .z_threshold   = 2.0,
                .min_samples   = 5.0
            }));
        rt.start();
    }
    void TearDown() override { rt.stop(); }
};

TEST_F(AnomalyDetectorTest, NoAlertsWithInsufficientSamples) {
    for (int i = 0; i < 4; ++i)
        rt.injectEvent(MetricEvent::cpu("h", 50.0));
    EXPECT_TRUE(rt.api().capturedEffectsOf<NotifyEffect>().empty());
}

TEST_F(AnomalyDetectorTest, DetectsOutlierAfterBaseline) {
    // Establish baseline (10 samples around 50%)
    for (int i = 0; i < 10; ++i)
        rt.injectEvent(MetricEvent::cpu("h", 50.0 + (i % 3)));

    rt.api().clearEffects();

    // Inject a spike
    rt.injectEvent(MetricEvent::cpu("h", 99.0));
    EXPECT_FALSE(rt.api().capturedEffectsOf<NotifyEffect>().empty());
}

TEST_F(AnomalyDetectorTest, NormalVariationDoesNotAlert) {
    // Baseline with some variance
    for (int i = 0; i < 20; ++i)
        rt.injectEvent(MetricEvent::cpu("h", 50.0 + (i % 10)));

    rt.api().clearEffects();

    // Inject a value within normal range
    rt.injectEvent(MetricEvent::cpu("h", 55.0));
    EXPECT_TRUE(rt.api().capturedEffectsOf<NotifyEffect>().empty());
}

// ─── RateLimiter ─────────────────────────────────────────────────────────────

class RateLimiterTest : public ::testing::Test {
protected:
    SimRuntime rt;

    void SetUp() override {
        rt.registerPipeline(makePipeline<RateLimiter>(
            "ratelimit",
            RateLimiter::Config{
                .metric_name   = "cpu.usage",
                .max_per_window = 2,
                .window         = Duration{5000}  // 5 seconds
            }));
        rt.start();
    }
    void TearDown() override { rt.stop(); }
};

TEST_F(RateLimiterTest, AllowsUpToMaxPerWindow) {
    rt.injectEvent(MetricEvent::cpu("h", 90.0));  // Allowed
    rt.injectEvent(MetricEvent::cpu("h", 91.0));  // Allowed
    EXPECT_TRUE(rt.api().capturedEffectsOf<SuppressEffect>().empty());
}

TEST_F(RateLimiterTest, SuppressesExcessEvents) {
    rt.injectEvent(MetricEvent::cpu("h", 90.0));
    rt.injectEvent(MetricEvent::cpu("h", 90.0));
    rt.injectEvent(MetricEvent::cpu("h", 90.0));  // 3rd in window → suppressed

    auto suppressed = rt.api().capturedEffectsOf<SuppressEffect>();
    EXPECT_FALSE(suppressed.empty());
}

TEST_F(RateLimiterTest, WindowResetAllowsNewEvents) {
    rt.injectEvent(MetricEvent::cpu("h", 90.0));
    rt.injectEvent(MetricEvent::cpu("h", 90.0));

    // Advance clock past the window
    rt.api().advanceClock(std::chrono::seconds(10));

    rt.api().clearEffects();
    rt.injectEvent(MetricEvent::cpu("h", 90.0));  // New window → allowed
    EXPECT_TRUE(rt.api().capturedEffectsOf<SuppressEffect>().empty());
}

// ─── MetricAggregator ─────────────────────────────────────────────────────────

class MetricAggregatorTest : public ::testing::Test {
protected:
    SimRuntime rt;

    void SetUp() override {
        rt.registerPipeline(makePipeline<MetricAggregator>(
            "agg",
            MetricAggregator::Config{
                .metric_name   = "http.latency_p99",  // matches MetricEvent::latency
                .flush_every_n = 5
            }));
        rt.start();
    }
    void TearDown() override { rt.stop(); }
};

TEST_F(MetricAggregatorTest, NoStoreBeforeFlushCount) {
    for (int i = 0; i < 4; ++i)
        rt.injectEvent(MetricEvent::latency("h", 10.0 * (i + 1)));
    EXPECT_TRUE(rt.api().capturedEffectsOf<StoreEffect>().empty());
}

TEST_F(MetricAggregatorTest, EmitsStoreOnFlush) {
    for (int i = 0; i < 5; ++i)
        rt.injectEvent(MetricEvent::latency("h", 10.0 * (i + 1)));

    auto stored = rt.api().capturedEffectsOf<StoreEffect>();
    ASSERT_EQ(stored.size(), 1u);
    EXPECT_EQ(stored[0].backend, StoreEffect::Backend::TIMESERIES);
    EXPECT_NE(stored[0].value.find("avg"), std::string::npos);
}

TEST_F(MetricAggregatorTest, StoredPayloadHasCorrectStats) {
    // 5 identical values → min=max=avg=10, p99=10
    for (int i = 0; i < 5; ++i)
        rt.injectEvent(MetricEvent::latency("h", 10.0));

    auto stored = rt.api().capturedEffectsOf<StoreEffect>();
    ASSERT_FALSE(stored.empty());
    EXPECT_NE(stored[0].value.find("\"min\":10"), std::string::npos);
    EXPECT_NE(stored[0].value.find("\"max\":10"), std::string::npos);
}

// ─── Replay ───────────────────────────────────────────────────────────────────

TEST(SimRuntimeReplayTest, ReplayIsFullyDeterministic) {
    std::vector<Event> events;
    for (int i = 0; i < 20; ++i)
        events.push_back(MetricEvent::cpu("host", static_cast<double>(i * 5)));
    // Last event: cpu.usage = 95 (above 90 threshold)

    auto run = [&]() -> std::vector<NotifyEffect> {
        SimRuntime rt;
        rt.registerPipeline(makePipeline<ThresholdDetector>(
            "pipe",
            ThresholdDetector::Config{"cpu.usage", 90.0}));
        rt.start();
        rt.replay(events);
        return rt.api().capturedEffectsOf<NotifyEffect>();
    };

    auto first  = run();
    auto second = run();
    auto third  = run();

    ASSERT_EQ(first.size(), second.size());
    ASSERT_EQ(first.size(), third.size());

    for (size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i].title,  second[i].title);
        EXPECT_EQ(first[i].title,  third[i].title);
        EXPECT_EQ(first[i].body,   second[i].body);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
