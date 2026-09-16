#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jdrive64 {

class SectorCache {
 public:
  explicit SectorCache(std::size_t capacity = 512);

  bool Get(std::uint8_t track, std::uint8_t sector, std::vector<std::uint8_t>* data_out);
  void Put(std::uint8_t track, std::uint8_t sector, const std::uint8_t* data, std::size_t size);

  std::size_t Hits() const;
  std::size_t Misses() const;

 private:
  struct CacheEntry {
    std::pair<std::uint8_t, std::uint8_t> key;
    std::vector<std::uint8_t> data;
  };

  struct PairHash {
    std::size_t operator()(const std::pair<std::uint8_t, std::uint8_t>& v) const;
  };

  using ListIt = std::list<CacheEntry>::iterator;

  std::size_t capacity_;
  std::list<CacheEntry> lru_;
  std::unordered_map<std::pair<std::uint8_t, std::uint8_t>, ListIt, PairHash> index_;
  std::size_t hits_ = 0;
  std::size_t misses_ = 0;
};

}  // namespace jdrive64
