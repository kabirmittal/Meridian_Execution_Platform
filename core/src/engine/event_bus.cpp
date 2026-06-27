#include "meridian/event_bus.hpp"
#include <algorithm>
#include <stdexcept>

namespace meridian {

EventBus::SubscriptionId EventBus::subscribe(Handler handler) {
    if (!handler) throw std::invalid_argument("EventBus::subscribe: null handler");
    std::lock_guard<std::mutex> lk(mutex_);
    auto id = next_id_.fetch_add(1, std::memory_order_relaxed);
    subscriptions_.push_back({id, std::move(handler)});
    return id;
}

bool EventBus::unsubscribe(SubscriptionId id) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = std::find_if(subscriptions_.begin(), subscriptions_.end(),
                            [id](const Subscription& s) { return s.id == id; });
    if (it == subscriptions_.end()) return false;
    subscriptions_.erase(it);
    return true;
}

size_t EventBus::publish(const Event& event) {
    // Apply global filter
    if (filter_) {
        if (!(*filter_)(event)) {
            filtered_count_.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
    }

    // Copy subscriptions under lock, then call handlers outside lock
    // (avoids deadlock if a handler calls subscribe/unsubscribe)
    std::vector<Handler> handlers;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        handlers.reserve(subscriptions_.size());
        for (const auto& s : subscriptions_)
            handlers.push_back(s.handler);
    }

    for (auto& h : handlers) h(event);

    if (!handlers.empty())
        published_count_.fetch_add(1, std::memory_order_relaxed);
    return handlers.size();
}

void EventBus::publishBatch(const std::vector<Event>& events) {
    // Copy handlers once for the whole batch
    std::vector<Handler> handlers;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        handlers.reserve(subscriptions_.size());
        for (const auto& s : subscriptions_)
            handlers.push_back(s.handler);
    }

    for (const auto& event : events) {
        if (filter_ && !(*filter_)(event)) {
            filtered_count_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        for (auto& h : handlers) h(event);
        published_count_.fetch_add(1, std::memory_order_relaxed);
    }
}

void EventBus::setFilter(Filter filter) {
    std::lock_guard<std::mutex> lk(mutex_);
    filter_ = std::move(filter);
}

void EventBus::clearFilter() {
    std::lock_guard<std::mutex> lk(mutex_);
    filter_.reset();
}

size_t EventBus::subscriberCount() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return subscriptions_.size();
}

void EventBus::resetCounters() {
    published_count_.store(0, std::memory_order_relaxed);
    dropped_count_.store(0, std::memory_order_relaxed);
    filtered_count_.store(0, std::memory_order_relaxed);
}

void EventBus::clear() {
    std::lock_guard<std::mutex> lk(mutex_);
    subscriptions_.clear();
}

} // namespace meridian
