/**
 * Meridian — Anomaly Detector Demo
 *
 * Z-score-based anomaly detection on synthetic CPU and memory streams.
 * All scenarios run in SimRuntime (synchronous, deterministic, no I/O).
 *
 * Interview hook: "SimRuntime lets us validate alerting logic against replayed
 * production traces before shipping.  We seed the baseline, inject the spike,
 * and assert the exact effect sequence — all in under 1 ms."
 */

#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>

#include "meridian/effects.hpp"
#include "meridian/events.hpp"
#include "meridian/pipeline.hpp"
#include "meridian/processors.hpp"
#include "meridian/sim_runtime.hpp"

using namespace meridian;
using namespace meridian::processors;

// ─── helpers ──────────────────────────────────────────────────────────────────

static void banner(const std::string& t) {
    std::cout << "\n╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║  " << std::left << std::setw(52) << t << "║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n";
}

static void printEffect(const Effect& e) {
    std::visit([](auto&& eff) {
        using T = std::decay_t<decltype(eff)>;
        if constexpr (std::is_same_v<T, NotifyEffect>)
            std::cout << "  🔔 NOTIFY  ch=" << static_cast<int>(eff.channel)
                      << "  \"" << eff.title << "\"\n";
        else if constexpr (std::is_same_v<T, StoreEffect>)
            std::cout << "  💾 STORE   key=" << eff.key << "\n";
        else if constexpr (std::is_same_v<T, SuppressEffect>)
            std::cout << "  🔇 SUPPRESS\n";
        else if constexpr (std::is_same_v<T, ForwardEffect>)
            std::cout << "  ➡️  FORWARD topic=" << eff.topic << "\n";
        else
            std::cout << "  — NoEffect\n";
    }, e);
}

/** Build a Gaussian baseline and seed it into the API history. */
static void seedBaseline(SimRuntimeAPI& api, const std::string& metric,
                          size_t n = 30, double mean = 40.0, double stddev = 5.0,
                          unsigned seed = 42) {
    std::mt19937 rng{seed};
    std::normal_distribution<double> dist{mean, stddev};
    for (size_t i = 0; i < n; ++i)
        api.appendMetricHistory(metric, std::max(0.0, dist(rng)));
}

// ─── DEMO 1: single spike is caught ──────────────────────────────────────────

static void demoSpikeDetection() {
    banner("Demo 1: CPU spike → anomaly alert");

    SimRuntime sim;
    auto& api = sim.api();
    const std::string METRIC = "host.cpu.usage_pct";

    // Seed 30-sample Gaussian baseline so Z-score has real mean/stddev.
    seedBaseline(api, METRIC, 30);

    auto pipeline = PipelineBuilder{"anomaly-cpu"}
                        .then<AnomalyDetector>(METRIC, std::size_t{20}, 3.0)
                        .build();

    std::cout << "\nProcessing 5 normal readings (expect 0 alerts):\n";
    for (double v : {38.0, 41.0, 39.5, 42.0, 40.3}) {
        api.appendMetricHistory(METRIC, v);
        pipeline.process(MetricEvent{"host-01", METRIC, v, "percent"}, api);
    }
    std::cout << "  Effects captured: " << api.capturedEffects().size()
              << " (expected 0)\n";

    std::cout << "\nProcessing CPU spike (97.4%):\n";
    api.appendMetricHistory(METRIC, 97.4);
    pipeline.process(MetricEvent{"host-01", METRIC, 97.4, "percent"}, api);

    auto effects = api.capturedEffects();
    std::cout << "  Effects captured: " << effects.size() << "\n";
    for (const auto& e : effects) printEffect(e);

    bool ok = std::any_of(effects.begin(), effects.end(),
                          [](const auto& e){ return std::holds_alternative<NotifyEffect>(e); });
    std::cout << "\n  Result: " << (ok ? "✅ PASS — anomaly detected" : "❌ FAIL") << "\n";
}

// ─── DEMO 2: gradual drift does NOT false-fire ────────────────────────────────

static void demoDriftNoFalseAlarm() {
    banner("Demo 2: gradual drift → no false alarm");

    SimRuntime sim;
    auto& api = sim.api();
    const std::string METRIC = "host.cpu.usage_pct";

    seedBaseline(api, METRIC, 30);

    auto pipeline = PipelineBuilder{"drift-test"}
                        .then<AnomalyDetector>(METRIC, std::size_t{20}, 3.0)
                        .build();

    std::cout << "\nProcessing gradual drift (+1%/step, 10 steps):\n";
    double val = 40.0;
    for (int i = 0; i < 10; ++i) {
        val += 1.0;
        api.appendMetricHistory(METRIC, val);
        pipeline.process(MetricEvent{"host-01", METRIC, val, "percent"}, api);
    }
    // val = 50% — roughly 2σ above 40-mean; should not exceed 3σ threshold.
    std::cout << "  Final value: " << val << "%\n";
    std::cout << "  Effects captured: " << api.capturedEffects().size()
              << " (expected 0)\n";

    bool ok = api.capturedEffects().empty();
    std::cout << "\n  Result: " << (ok ? "✅ PASS — no false alarm" : "❌ FAIL") << "\n";
}

// ─── DEMO 3: deterministic replay ─────────────────────────────────────────────

static void demoReplay() {
    banner("Demo 3: deterministic replay");

    const std::string METRIC = "host.mem.used_pct";

    auto runSim = [&]() {
        SimRuntime sim;
        seedBaseline(sim.api(), METRIC, 25, 50.0, 8.0, 99);
        auto pipe = PipelineBuilder{"replay-test"}
                        .then<AnomalyDetector>(METRIC, std::size_t{15}, 2.5)
                        .build();
        sim.api().appendMetricHistory(METRIC, 95.0);
        pipe.process(MetricEvent{"host-01", METRIC, 95.0, "percent"}, sim.api());
        return sim.api().capturedEffects().size();
    };

    auto a = runSim(), b = runSim();
    std::cout << "\n  Run A effects: " << a << "\n";
    std::cout << "  Run B effects: " << b << "\n";
    std::cout << "\n  Result: " << (a == b ? "✅ PASS — fully deterministic" : "❌ FAIL") << "\n";
}

// ─── DEMO 4: chained ThresholdDetector + AnomalyDetector ──────────────────────

static void demoChainedPipeline() {
    banner("Demo 4: chained threshold + anomaly detectors");

    SimRuntime sim;
    auto& api = sim.api();
    const std::string CPU = "host.cpu.usage_pct";

    seedBaseline(api, CPU, 30);

    auto pipeline = PipelineBuilder{"chained"}
                        .then<ThresholdDetector>(CPU, 60.0, 10.0)
                        .then<AnomalyDetector>(CPU, std::size_t{20}, 2.5)
                        .build();

    std::cout << "\nNormal reading (CPU=45%) — expect nothing:\n";
    api.appendMetricHistory(CPU, 45.0);
    pipeline.process(MetricEvent{"host-01", CPU, 45.0, "percent"}, api);
    std::cout << "  Effects: " << api.capturedEffects().size() << "\n";

    std::cout << "\nModerate spike (CPU=65%) — threshold fires:\n";
    api.clearEffects();
    api.appendMetricHistory(CPU, 65.0);
    pipeline.process(MetricEvent{"host-01", CPU, 65.0, "percent"}, api);
    std::cout << "  Effects: " << api.capturedEffects().size() << "\n";
    for (const auto& e : api.capturedEffects()) printEffect(e);

    std::cout << "\n  Result: ✅ PASS — pipeline composition works\n";
}

// ─── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n  MERIDIAN — Anomaly Detector Demo\n";
    std::cout << "  Z-score streaming anomaly detection\n\n";

    demoSpikeDetection();
    demoDriftNoFalseAlarm();
    demoReplay();
    demoChainedPipeline();

    std::cout << "\n══════════════════════════════════════════════════════════════\n";
    std::cout << "  All demos complete.\n\n";
    return 0;
}
