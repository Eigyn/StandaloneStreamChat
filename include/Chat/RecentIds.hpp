#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <unordered_set>

namespace StandaloneStreamChat::Chat {
    /// Bounded, thread-safe set of recently seen message ids, oldest evicted
    /// first. Used to drop duplicates across subscriptions and reconnects.
    class RecentIds {
       public:
        explicit RecentIds(size_t capacity) : _capacity(capacity) {}

        /// Returns false if `id` was already seen.
        bool Insert(std::string const& id) {
            std::lock_guard lock(_lock);
            if (!_ids.insert(id).second)
                return false;
            _order.push_back(id);
            while (_order.size() > _capacity) {
                _ids.erase(_order.front());
                _order.pop_front();
            }
            return true;
        }

       private:
        size_t _capacity;
        std::mutex _lock;
        std::unordered_set<std::string> _ids;
        std::deque<std::string> _order;
    };
}
