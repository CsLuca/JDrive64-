#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "jdrive64/bam_reader.hpp"
#include "jdrive64/d64_reader.hpp"
#include "jdrive64/disk_catalog.hpp"
#include "jdrive64/file_cache.hpp"
#include "jdrive64/sector_cache.hpp"

namespace jdrive64 {

class DiskImageSession {
 public:
  struct CacheStats {
    std::size_t hits = 0;
    std::size_t misses = 0;
    std::size_t size = 0;
    std::size_t capacity = 0;
    double hit_rate = 0.0;
  };

  struct RuntimeStats {
    CacheStats sector_cache;
    CacheStats file_cache;
    std::uint64_t bytes_served = 0;
    std::uint64_t read_ops = 0;
    std::uint64_t open_count = 0;
    double avg_read_latency_us = 0.0;
    double throughput_bytes_per_sec = 0.0;
    std::size_t file_cache_max_item_size = 0;
  };

  bool Open(const std::string& image_path);
  void ConfigureCaches(std::size_t sector_cache_capacity,
                       std::size_t file_cache_capacity,
                       std::size_t file_cache_max_item_size);

  const BAMReader& Bam() const;
  const DiskCatalog& Catalog() const;

  bool ReadFileByWindowsName(const std::string& windows_name, std::vector<std::uint8_t>* data);
  bool ReadFileByCatalogFile(const CatalogFile& file, std::vector<std::uint8_t>* data);

  RuntimeStats GetRuntimeStats() const;
  void ResetRuntimeStats();

  const std::string& LastError() const;

 private:
  D64Reader reader_;
  BAMReader bam_;
  DiskCatalog catalog_;
  SectorCache sector_cache_;
  FileCache file_cache_;
  std::uint64_t bytes_served_ = 0;
  std::uint64_t read_ops_ = 0;
  std::uint64_t open_count_ = 0;
  std::uint64_t total_read_latency_us_ = 0;
  std::chrono::steady_clock::time_point first_read_time_{};
  bool has_first_read_time_ = false;
  std::size_t file_cache_max_item_size_ = 64 * 1024;
  std::string last_error_;
};

}  // namespace jdrive64
