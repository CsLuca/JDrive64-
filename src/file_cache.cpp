#include "jdrive64/file_cache.hpp"

namespace jdrive64 {

FileCache::FileCache(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

bool FileCache::Get(const std::string& key, std::vector<std::uint8_t>* data_out) {
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

void FileCache::Put(std::string key, std::vector<std::uint8_t> data) {
  const auto existing = index_.find(key);
  if (existing != index_.end()) {
    existing->second->data = std::move(data);
    lru_.splice(lru_.begin(), lru_, existing->second);
    return;
  }

  CacheEntry entry;
  entry.key = std::move(key);
  entry.data = std::move(data);
  lru_.push_front(std::move(entry));
  index_[lru_.front().key] = lru_.begin();

  if (lru_.size() > capacity_) {
    index_.erase(lru_.back().key);
    lru_.pop_back();
  }
}

std::size_t FileCache::Hits() const { return hits_; }

std::size_t FileCache::Misses() const { return misses_; }

std::size_t FileCache::Capacity() const { return capacity_; }

std::size_t FileCache::Size() const { return lru_.size(); }

double FileCache::HitRate() const {
  const auto total = hits_ + misses_;
  if (total == 0) {
    return 0.0;
  }
  return static_cast<double>(hits_) / static_cast<double>(total);
}

void FileCache::ResetStats() {
  hits_ = 0;
  misses_ = 0;
}

}  // namespace jdrive64
