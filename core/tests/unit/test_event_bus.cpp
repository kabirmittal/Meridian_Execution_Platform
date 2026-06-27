#include "meridian/event_bus.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <vector>

using namespace meridian;

class EventBusTest : public ::testing::Test {
protected:
    EventBus bus;
};

// ─── Subscription ─────────────────────────────────────────────────────────────

TEST_F(EventBusTest, SubscribeIncreasesCount) {
    EXPECT_EQ(bus.subscriberCount(), 0u);
    bus.subscribe([](const Event&) {});
    EXPECT_EQ(bus.subscriberCount(), 1u);
    bus.subscribe([](const Event&) {});
    EXPECT_EQ(bus.subscriberCount(), 2u);
}

TEST_F(EventBusTest, SubscribeThrowsOnNullHandler) {
    EXPECT_THROW(bus.subscribe(nullptr), std::invalid_argument);
}

TEST_F(EventBusTest, UnsubscribeRemovesHandler) {
    auto id = bus.subscribe([](const Event&) {});
    EXPECT_TRUE(bus.unsubscribe(id));
    EXPECT_EQ(bus.subscriberCount(), 0u);
}

TEST_F(EventBusTest, UnsubscribeReturnsFalseForUnknownId) {
    EXPECT_FALSE(bus.unsubscribe(9999));
}

// ─── Publishing ───────────────────────────────────────────────────────────────

TEST_F(EventBusTest, PublishCallsAllSubscribers) {
    std::atomic<int> calls{0};
    bus.subscribe([&](const Event&) { ++calls; });
    bus.subscribe([&](const Event&) { ++calls; });
    bus.subscribe([&](const Event&) { ++calls; });

    bus.publish(MetricEvent::cpu("host-1", 42.0));
    EXPECT_EQ(calls.load(), 3);
}

TEST_F(EventBusTest, PublishReturnsSubscriberCount) {
    bus.subscribe([](const Event&) {});
    bus.subscribe([](const Event&) {});
    size_t notified = bus.publish(MetricEvent::cpu("src", 1.0));
    EXPECT_EQ(notified, 2u);
}

TEST_F(EventBusTest, PublishWithNoSubscribersIsNoop) {
    EXPECT_EQ(bus.publish(MetricEvent::cpu("src", 1.0)), 0u);
    EXPECT_EQ(bus.publishedCount(), 0u);  // filtered=0, so published should be 0 too
}

TEST_F(EventBusTest, PublishCountIncrementsCorrectly) {
    bus.subscribe([](const Event&) {});
    bus.publish(MetricEvent::cpu("src", 1.0));
    bus.publish(MetricEvent::cpu("src", 2.0));
    bus.publish(MetricEvent::cpu("src", 3.0));
    EXPECT_EQ(bus.publishedCount(), 3u);
}

// ─── Type-filtered subscription ───────────────────────────────────────────────

TEST_F(EventBusTest, TypeFilteredSubscribeOnlyReceivesMatchingType) {
    int metric_count = 0;
    int log_count    = 0;

    bus.subscribeToType<MetricEvent>([&](const MetricEvent&) { ++metric_count; });
    bus.subscribeToType<LogEvent>   ([&](const LogEvent&)    { ++log_count; });

    bus.publish(MetricEvent::cpu("h", 50.0));
    bus.publish(MetricEvent::cpu("h", 60.0));
    bus.publish(LogEvent{"h", LogEvent::Level::WARN, "something"});

    EXPECT_EQ(metric_count, 2);
    EXPECT_EQ(log_count, 1);
}

TEST_F(EventBusTest, HeartbeatSubscriberDoesNotReceiveMetrics) {
    int hb_count = 0;
    bus.subscribeToType<HeartbeatEvent>([&](const HeartbeatEvent&) { ++hb_count; });

    bus.publish(MetricEvent::cpu("h", 42.0));
    bus.publish(LogEvent{"h", LogEvent::Level::INFO, "hello"});

    EXPECT_EQ(hb_count, 0);

    bus.publish(HeartbeatEvent{"svc", "host", true});
    EXPECT_EQ(hb_count, 1);
}

// ─── Event ordering ───────────────────────────────────────────────────────────

TEST_F(EventBusTest, SubscribersCalledInRegistrationOrder) {
    std::vector<int> order;
    bus.subscribe([&](const Event&) { order.push_back(1); });
    bus.subscribe([&](const Event&) { order.push_back(2); });
    bus.subscribe([&](const Event&) { order.push_back(3); });

    bus.publish(MetricEvent::cpu("h", 1.0));
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

// ─── Filtering ────────────────────────────────────────────────────────────────

TEST_F(EventBusTest, GlobalFilterDropsEvents) {
    int calls = 0;
    bus.subscribe([&](const Event&) { ++calls; });

    // Filter: only allow MetricEvents with value > 90
    bus.setFilter([](const Event& e) {
        const auto* me = std::get_if<MetricEvent>(&e);
        return me && me->value > 90.0;
    });

    bus.publish(MetricEvent::cpu("h", 50.0));   // Dropped
    bus.publish(MetricEvent::cpu("h", 95.0));   // Passed
    bus.publish(LogEvent{"h", LogEvent::Level::INFO, "log"});  // Dropped

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(bus.filteredCount(), 2u);
}

TEST_F(EventBusTest, ClearFilterRestoresNormalBehavior) {
    int calls = 0;
    bus.subscribe([&](const Event&) { ++calls; });
    bus.setFilter([](const Event&) { return false; });  // Drop everything

    bus.publish(MetricEvent::cpu("h", 1.0));
    EXPECT_EQ(calls, 0);

    bus.clearFilter();
    bus.publish(MetricEvent::cpu("h", 1.0));
    EXPECT_EQ(calls, 1);
}

// ─── Batch publishing ─────────────────────────────────────────────────────────

TEST_F(EventBusTest, PublishBatchDeliverAllEvents) {
    int count = 0;
    bus.subscribe([&](const Event&) { ++count; });

    std::vector<Event> batch;
    for (int i = 0; i < 10; ++i)
        batch.push_back(MetricEvent::cpu("h", static_cast<double>(i)));

    bus.publishBatch(batch);
    EXPECT_EQ(count, 10);
}

// ─── Thread safety ────────────────────────────────────────────────────────────

TEST_F(EventBusTest, ConcurrentPublishIsThreadSafe) {
    std::atomic<int> calls{0};
    bus.subscribe([&](const Event&) { ++calls; });

    constexpr int N_THREADS = 8;
    constexpr int N_EVENTS  = 100;

    std::vector<std::thread> threads;
    threads.reserve(N_THREADS);
    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < N_EVENTS; ++i)
                bus.publish(MetricEvent::cpu("h", static_cast<double>(i)));
        });
    }
    for (auto& th : threads) th.join();

    EXPECT_EQ(calls.load(), N_THREADS * N_EVENTS);
    EXPECT_EQ(bus.publishedCount(), static_cast<uint64_t>(N_THREADS * N_EVENTS));
}

// ─── Clear ────────────────────────────────────────────────────────────────────

TEST_F(EventBusTest, ClearRemovesAllSubscriptions) {
    bus.subscribe([](const Event&) {});
    bus.subscribe([](const Event&) {});
    bus.clear();
    EXPECT_EQ(bus.subscriberCount(), 0u);
}

TEST_F(EventBusTest, ResetCountersZerosAll) {
    bus.subscribe([](const Event&) {});
    bus.publish(MetricEvent::cpu("h", 1.0));
    EXPECT_GT(bus.publishedCount(), 0u);
    bus.resetCounters();
    EXPECT_EQ(bus.publishedCount(), 0u);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
