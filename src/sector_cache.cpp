#include "jdrive64/sector_cache.hpp"

namespace jdrive64 {

SectorCache::SectorCache(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

bool SectorCache::Get(std::uint8_t track, std::uint8_t sector, std::vector<std::uint8_t>* data_out) {
  const auto key = std::make_pair(track, sector);
  const auto it = index_.find(key);
  if (it == index_.end()) {
    ++misses_;
    return false;
  }

  lru_.splice(lru_.begin(), lru_, it->second);
  if (data_out != nullptr) {
    *data_out = it->second->data;
  }

  ++hits_;
  return true;
}

void SectorCache::Put(std::uint8_t track, std::uint8_t sector, const std::uint8_t* data, std::size_t size) {
  const auto key = std::make_pair(track, sector);
  const auto existing = index_.find(key);
  if (existing != index_.end()) {
    existing->second->data.assign(data, data + size);
    lru_.splice(lru_.begin(), lru_, existing->second);
    return;
  }

  CacheEntry entry;
  entry.key = key;
  entry.data.assign(data, data + size);
  lru_.push_front(std::move(entry));
  index_[key] = lru_.begin();

  if (lru_.size() > capacity_) {
    const auto& to_remove = lru_.back().key;
    index_.erase(to_remove);
    lru_.pop_back();
  }
}

std::size_t SectorCache::Hits() const { return hits_; }

std::size_t SectorCache::Misses() const { return misses_; }

std::size_t SectorCache::Capacity() const { return capacity_; }

std::size_t SectorCache::Size() const { return lru_.size(); }

double SectorCache::HitRate() const {
  const auto total = hits_ + misses_;
  if (total == 0) {
    return 0.0;
  }
  return static_cast<double>(hits_) / static_cast<double>(total);
}

void SectorCache::ResetStats() {
  hits_ = 0;
  misses_ = 0;
}

void SectorCache::SetCapacity(std::size_t capacity) {
  capacity_ = capacity == 0 ? 1 : capacity;
  while (lru_.size() > capacity_) {
    const auto& to_remove = lru_.back().key;
    index_.erase(to_remove);
    lru_.pop_back();
  }
}

std::size_t SectorCache::PairHash::operator()(
    const std::pair<std::uint8_t, std::uint8_t>& v) const {
  return (static_cast<std::size_t>(v.first) << 8U) ^ static_cast<std::size_t>(v.second);
}

}  // namespace jdrive64
