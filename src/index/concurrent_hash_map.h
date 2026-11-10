#pragma once

#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace indexing {

// Hash table string -> V with lock striping.
//
// The key space is split into independent segments, each one an
// open-addressing table (linear probing) with its own shared_mutex.
// Writers lock one segment exclusively, readers lock it in shared mode,
// so threads that touch different segments never block each other.
template <class V>
class ConcurrentHashMap {
 public:
  ConcurrentHashMap(size_t segments, size_t capacity) : segments_(segments ? segments : 1) {
    const size_t per_segment = capacity / segments_.size() + 1;
    for (auto& segment : segments_) segment.slots.resize(RoundUpPow2(per_segment * 2));
  }

  // Applies fn(V&) to the value for `key`, inserting V{} first if needed.
  template <class F>
  void Upsert(std::string_view key, F&& fn) {
    const uint64_t h = Hash(key);
    Segment& segment = SegmentFor(h);
    std::unique_lock lock(segment.mutex);
    if ((segment.size + 1) * 10 > segment.slots.size() * 7) segment.Grow();  // load <= 0.7
    Slot& slot = segment.slots[segment.Probe(key, h)];
    if (!slot.used) {
      slot.used = true;
      slot.hash = h;
      slot.key.assign(key);
      ++segment.size;
    }
    fn(slot.value);
  }

  // Calls fn(const V&) under a shared lock; returns false if `key` is absent.
  template <class F>
  bool Find(std::string_view key, F&& fn) const {
    const uint64_t h = Hash(key);
    const Segment& segment = segments_[SegmentIndex(h)];
    std::shared_lock lock(segment.mutex);
    const Slot& slot = segment.slots[segment.Probe(key, h)];
    if (!slot.used) return false;
    fn(slot.value);
    return true;
  }

  // Calls fn(key, V&) for every entry of one segment (used to parallelize
  // whole-table passes: one task per segment).
  template <class F>
  void ForEachInSegment(size_t segment_index, F&& fn) {
    Segment& segment = segments_[segment_index];
    std::unique_lock lock(segment.mutex);
    for (auto& slot : segment.slots) {
      if (slot.used) fn(std::string_view(slot.key), slot.value);
    }
  }

  size_t Size() const {
    size_t total = 0;
    for (const auto& segment : segments_) {
      std::shared_lock lock(segment.mutex);
      total += segment.size;
    }
    return total;
  }

  size_t segment_count() const { return segments_.size(); }

 private:
  struct Slot {
    uint64_t hash = 0;
    std::string key;
    V value{};
    bool used = false;
  };

  // alignas(64): each segment's mutex lives on its own cache line,
  // which avoids false sharing between threads locking neighbour segments.
  struct alignas(64) Segment {
    mutable std::shared_mutex mutex;
    std::vector<Slot> slots;
    size_t size = 0;

    // Index of the slot holding `key`, or of the empty slot where it belongs.
    size_t Probe(std::string_view key, uint64_t h) const {
      const size_t mask = slots.size() - 1;
      for (size_t i = (h >> 16) & mask;; i = (i + 1) & mask) {
        const Slot& slot = slots[i];
        if (!slot.used || (slot.hash == h && slot.key == key)) return i;
      }
    }

    void Grow() {
      std::vector<Slot> old = std::exchange(slots, std::vector<Slot>(slots.size() * 2));
      for (auto& slot : old) {
        if (!slot.used) continue;
        slots[Probe(slot.key, slot.hash)] = std::move(slot);
      }
    }
  };

  // FNV-1a 64-bit.
  static uint64_t Hash(std::string_view key) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : key) h = (h ^ c) * 1099511628211ull;
    return h;
  }

  static size_t RoundUpPow2(size_t n) {
    size_t p = 8;
    while (p < n) p <<= 1;
    return p;
  }

  size_t SegmentIndex(uint64_t h) const { return (h >> 48) % segments_.size(); }
  Segment& SegmentFor(uint64_t h) { return segments_[SegmentIndex(h)]; }

  std::vector<Segment> segments_;
};

}  // namespace indexing