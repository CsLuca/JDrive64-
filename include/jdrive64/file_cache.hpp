#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace jdrive64 {

class FileCache {
 public:
  explicit FileCache(std::size_t capacity = 128);

  bool Get(const std::string& key, std::vector<std::uint8_t>* data_out);
  void Put(std::string key, std::vector<std::uint8_t> data);

  std::size_t Hits() const;
  std::size_t Misses() const;

 private:
  struct CacheEntry {
    std::string key;
    std::vector<std::uint8_t> data;
  };

  using ListIt = std::list<CacheEntry>::iterator;

  std::size_t capacity_;
  std::list<CacheEntry> lru_;
  std::unordered_map<std::string, ListIt> index_;
  std::size_t hits_ = 0;
  std::size_t misses_ = 0;
};

}  // namespace jdrive64
