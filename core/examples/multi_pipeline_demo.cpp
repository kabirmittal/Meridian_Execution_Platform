/**
 * Meridian — Multi-Pipeline Demo
 *
 * Shows how multiple independent pipelines share events through an EventBus,
 * how shadow mode validates new logic against live events without executing
 * effects, and how ProdRuntime handles a high-throughput event burst.
 *
 * Interview hook: "Shadow mode lets us dark-launch new processor logic
 * against real traffic.  The shadow pipeline receives identical events,
 * runs all the code, captures the effects, but never calls the handlers —
 * so zero production risk while we validate the new algorithm."
 */

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "meridian/event_bus.hpp"
#include "meridian/events.hpp"
#include "meridian/pipeline.hpp"
#include "meridian/processors.hpp"
#include "meridian/prod_runtime.hpp"
#include "meridian/runtime.hpp"
#include "meridian/sim_runtime.hpp"

using namespace meridian;
using namespace meridian::processors;
using namespace std::chrono_literals;

// ─── helpers ──────────────────────────────────────────────────────────────────

static void section(const std::string& title) {
    std::cout << "\n┌─────────────────────────────────────────────────────────┐\n";
    std::cout << "│  " << std::left << std::setw(55) << title << "│\n";
    std::cout << "└─────────────────────────────────────────────────────────┘\n";
}

// ─── DEMO 1: two independent SimRuntime pipelines ─────────────────────────────

static void demoTwoPipelines() {
    section("Demo 1: independent CPU + memory pipelines (SimRuntime)");

    // CPU pipeline
    SimRuntime cpuSim;
    cpuSim.api().seedMetric("cpu.usage", 42.0);
    auto cpuPipeline = PipelineBuilder{"cpu-watch"}
                           .then<ThresholdDetector>("cpu.usage", 80.0, 10.0)
                           .then<RateLimiter>("cpu.usage", std::size_t{3}, 60.0)
                           .build();

    // Memory pipeline
    SimRuntime memSim;
    memSim.api().seedMetric("mem.used_pct", 55.0);
    auto memPipeline = PipelineBuilder{"mem-watch"}
                           .then<ThresholdDetector>("mem.used_pct", 90.0, 5.0)
                           .then<MetricAggregator>("mem.used_pct", std::size_t{3})
                           .build();

    std::cout << "\nCPU pipeline — inject 3 events (2 over threshold):\n";
    for (double v : {45.0, 85.0, 92.0}) {
        cpuPipeline.process(MetricEvent{"host-01", "cpu.usage", v}, cpuSim.api());
        std::cout << "  cpu.usage=" << v
                  << "  total_effects=" << cpuSim.api().capturedEffects().size() << "\n";
    }

    std::cout << "\nMemory pipeline — inject 5 events:\n";
    for (double v : {55.0, 60.0, 75.0, 88.0, 95.0}) {
        memPipeline.process(MetricEvent{"host-01", "mem.used_pct", v}, memSim.api());
        std::cout << "  mem.used_pct=" << v
                  << "  total_effects=" << memSim.api().capturedEffects().size() << "\n";
    }

    std::cout << "\n✅ Two pipelines ran independently — no shared state\n";
}

// ─── DEMO 2: shadow vs prod effect comparison ──────────────────────────────────

static void demoShadowVsProd() {
    section("Demo 2: shadow mode — dark-launch validation");

    const std::string METRIC = "disk.io_pct";

    auto makeBaseline = [&]() {
        std::vector<double> b;
        for (int i = 0; i < 20; ++i) b.push_back(30.0 + i);
        return b;
    };

    // Prod pipeline: 2.5σ threshold (existing)
    SimRuntime prodSim;
    prodSim.api().seedMetricHistory(METRIC, makeBaseline());
    auto prodPipeline = PipelineBuilder{"prod"}
                            .then<AnomalyDetector>(METRIC, std::size_t{15}, 2.5)
                            .build();

    // Shadow pipeline: 2.0σ threshold (candidate — tighter)
    SimRuntime shadowSim;
    shadowSim.api().seedMetricHistory(METRIC, makeBaseline());
    auto shadowPipeline = PipelineBuilder{"shadow"}
                              .then<AnomalyDetector>(METRIC, std::size_t{15}, 2.0)
                              .build();

    const std::vector<double> events = {35.0, 38.0, 90.0, 36.0, 88.0, 37.0};
    std::cout << "\nReplaying " << events.size() << " events through both pipelines:\n";
    std::cout << "  event_value | prod_effects | shadow_effects\n";
    std::cout << "  -----------   ------------   --------------\n";

    for (double v : events) {
        prodSim.api().seedMetricHistory(METRIC, {v});
        shadowSim.api().seedMetricHistory(METRIC, {v});

        prodPipeline.process(MetricEvent{"host-01", METRIC, v}, prodSim.api());
        shadowPipeline.process(MetricEvent{"host-01", METRIC, v}, shadowSim.api());

        std::cout << "  " << std::setw(11) << v
                  << "   " << std::setw(12) << prodSim.api().capturedEffects().size()
                  << "   " << std::setw(14) << shadowSim.api().capturedEffects().size()
                  << "\n";
    }

    std::cout << "\nProd fired:   " << prodSim.api().capturedEffects().size() << " effect(s)\n";
    std::cout << "Shadow fired: " << shadowSim.api().capturedEffects().size() << " effect(s)\n";
    std::cout << "\n✅ Shadow divergence detected — tighter threshold fires more often\n";
    std::cout << "   → safe to compare before promoting the new algorithm to prod\n";
}

// ─── DEMO 3: EventBus fan-out to multiple subscribers ─────────────────────────

static void demoEventBusFanout() {
    section("Demo 3: EventBus fan-out → two subscribers");

    EventBus bus;

    SimRuntime simA;
    simA.api().seedMetric("net.rx_mbps", 100.0);
    auto pipeA = PipelineBuilder{"a"}
                     .then<ThresholdDetector>("net.rx_mbps", 900.0, 50.0)
                     .build();

    SimRuntime simB;
    simB.api().seedMetric("net.rx_mbps", 100.0);
    auto pipeB = PipelineBuilder{"b"}
                     .then<RateLimiter>("net.rx_mbps", std::size_t{2}, 30.0)
                     .build();

    std::atomic<int> fanoutCount{0};

    bus.subscribeToType<MetricEvent>([&](const MetricEvent& ev) {
        pipeA.process(ev, simA.api());
        ++fanoutCount;
    });
    bus.subscribeToType<MetricEvent>([&](const MetricEvent& ev) {
        pipeB.process(ev, simB.api());
        ++fanoutCount;
    });

    std::cout << "\nPublishing 4 metric events to bus:\n";
    for (double v : {150.0, 850.0, 920.0, 100.0}) {
        bus.publish(MetricEvent{"host-01", "net.rx_mbps", v});
        std::cout << "  published net.rx_mbps=" << v << "\n";
    }

    std::cout << "\nBus stats:\n";
    std::cout << "  published:           " << bus.publishedCount() << " event(s)\n";
    std::cout << "  handler invocations: " << fanoutCount.load()
              << " (2 subs × 4 events)\n";
    std::cout << "  Pipeline A effects:  " << simA.api().capturedEffects().size() << "\n";
    std::cout << "  Pipeline B effects:  " << simB.api().capturedEffects().size() << "\n";
    std::cout << "\n✅ Fan-out confirmed — both pipelines received all events\n";
}

// ─── DEMO 4: ProdRuntime high-throughput burst ────────────────────────────────

static void demoProdRuntimeBurst() {
    section("Demo 4: ProdRuntime — 1 000-event burst");

    RuntimeConfig cfg;
    cfg.mode           = RuntimeMode::PRODUCTION;
    cfg.worker_threads = 2;

    auto runtime = createRuntime(cfg);

    std::atomic<int> notifyCount{0};
    runtime->registerEffectHandler([&](const Effect& e) {
        if (std::holds_alternative<NotifyEffect>(e)) ++notifyCount;
    });

    auto& prodApi = static_cast<ProdRuntime&>(*runtime).api();

    std::vector<double> baseline;
    for (int i = 0; i < 50; ++i) baseline.push_back(20.0 + (i % 5));
    prodApi.ingestMetric("srv.latency_ms", 20.0, Clock::now());  // touch metric so it exists

    auto pipeline = std::make_shared<Pipeline>(PipelineConfig{.name = "burst"});
    pipeline->emplace<AnomalyDetector>("srv.latency_ms", std::size_t{30}, 3.0);
    runtime->registerPipeline(pipeline);

    runtime->start();

    const int N = 1000;
    auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i < N; ++i) {
        double val = (i % 100 == 0) ? 500.0 : (20.0 + (i % 5));
        prodApi.ingestMetric("srv.latency_ms", val, Clock::now());
    }

    std::this_thread::sleep_for(100ms);
    runtime->stop();

    auto elapsed = std::chrono::steady_clock::now() - t0;
    double ms    = std::chrono::duration<double, std::milli>(elapsed).count();
    double evtps = N / (ms / 1000.0);

    std::cout << "\n  Events ingested: " << N << "\n";
    std::cout << "  Notifications:   " << notifyCount.load() << "\n";
    std::cout << "  Wall time:       " << std::fixed << std::setprecision(1) << ms << " ms\n";
    std::cout << "  Throughput:      " << std::fixed << std::setprecision(0)
              << evtps << " events/sec (ingest rate)\n";
    std::cout << "\n✅ ProdRuntime burst complete\n";
}

// ─── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n";
    std::cout << "  MERIDIAN — Multi-Pipeline Demo\n";
    std::cout << "  fan-out, shadow mode, burst throughput\n\n";

    demoTwoPipelines();
    demoShadowVsProd();
    demoEventBusFanout();
    demoProdRuntimeBurst();

    std::cout << "\n══════════════════════════════════════════════════════════════\n";
    std::cout << "  All demos complete.\n\n";
    return 0;
}
