#pragma once
#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <string>

namespace SimpleBSStreamChat::UI {
    // Main-thread row lifetimes, independent of Unity so eviction can be tested.
    class EmoteReferences {
       public:
        void Begin(uint64_t row) { Release(row); _current = row; _tracking = true; }
        void End() { _tracking = false; }
        bool Use(std::string const& key) {
            if (!_tracking) return false;
            auto& keys = _rows[_current];
            if (keys.contains(key)) return true;
            if (keys.size() >= 32) return false;
            keys.insert(key); ++_counts[key];
            return true;
        }
        bool Contains(std::string const& key) const { return _counts.contains(key); }
        void Retain(std::span<uint64_t const> rows) {
            std::unordered_set<uint64_t> keep(rows.begin(), rows.end());
            for (auto it = _rows.begin(); it != _rows.end();) {
                auto row = it->first; ++it;
                if (!keep.contains(row)) Release(row);
            }
        }
       private:
        void Release(uint64_t row) {
            auto it = _rows.find(row);
            if (it == _rows.end()) return;
            for (auto const& key : it->second) {
                auto count = _counts.find(key);
                if (count != _counts.end() && --count->second == 0) _counts.erase(count);
            }
            _rows.erase(it);
        }
        bool _tracking = false;
        uint64_t _current = 0;
        std::unordered_map<uint64_t, std::unordered_set<std::string>> _rows;
        std::unordered_map<std::string, size_t> _counts;
    };
}
