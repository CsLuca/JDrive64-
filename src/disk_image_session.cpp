#include "jdrive64/disk_image_session.hpp"

#include <chrono>

#include "jdrive64/file_chain_reader.hpp"

namespace jdrive64 {

bool DiskImageSession::Open(const std::string& image_path) {
  last_error_.clear();
  ++open_count_;

  reader_.SetSectorCache(&sector_cache_);
  if (!reader_.Open(image_path)) {
    last_error_ = reader_.LastError();
    return false;
  }

  if (!bam_.Load(reader_)) {
    last_error_ = bam_.LastError();
    return false;
  }

  if (!catalog_.Build(reader_)) {
    last_error_ = catalog_.LastError();
    return false;
  }

  return true;
}

void DiskImageSession::ConfigureCaches(std::size_t sector_cache_capacity,
                                       std::size_t file_cache_capacity,
                                       std::size_t file_cache_max_item_size) {
  sector_cache_.SetCapacity(sector_cache_capacity);
  file_cache_.SetCapacity(file_cache_capacity);
  file_cache_max_item_size_ = file_cache_max_item_size;
}

const BAMReader& DiskImageSession::Bam() const { return bam_; }

const DiskCatalog& DiskImageSession::Catalog() const { return catalog_; }

bool DiskImageSession::ReadFileByWindowsName(const std::string& windows_name,
                                             std::vector<std::uint8_t>* data) {
  last_error_.clear();
  const auto start = std::chrono::steady_clock::now();
  if (!has_first_read_time_) {
    first_read_time_ = start;
    has_first_read_time_ = true;
  }
  if (data == nullptr) {
    last_error_ = "Invalid output buffer";
    return false;
  }

  if (file_cache_.Get(windows_name, data)) {
    const auto end = std::chrono::steady_clock::now();
    total_read_latency_us_ += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    bytes_served_ += static_cast<std::uint64_t>(data->size());
    ++read_ops_;
    return true;
  }

  const auto* file = catalog_.FindByWindowsName(windows_name);
  if (file == nullptr) {
    last_error_ = "File not found";
    return false;
  }

  return ReadFileByCatalogFile(*file, data);
}

bool DiskImageSession::ReadFileByCatalogFile(const CatalogFile& file, std::vector<std::uint8_t>* data) {
  last_error_.clear();
  const auto start = std::chrono::steady_clock::now();
  if (!has_first_read_time_) {
    first_read_time_ = start;
    has_first_read_time_ = true;
  }
  if (data == nullptr) {
    last_error_ = "Invalid output buffer";
    return false;
  }

  const std::string windows_name = file.windows_name;
  if (file_cache_.Get(windows_name, data)) {
    const auto end = std::chrono::steady_clock::now();
    total_read_latency_us_ += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    bytes_served_ += static_cast<std::uint64_t>(data->size());
    ++read_ops_;
    return true;
  }

  FileChainReader reader(reader_);
  auto bytes = reader.ReadFile(file);
  if (!reader.LastError().empty()) {
    last_error_ = reader.LastError();
    return false;
  }

  if (bytes.size() <= file_cache_max_item_size_) {
    file_cache_.Put(windows_name, bytes);
  }
  *data = std::move(bytes);
  const auto end = std::chrono::steady_clock::now();
  total_read_latency_us_ += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  bytes_served_ += static_cast<std::uint64_t>(data->size());
  ++read_ops_;
  return true;
}

const std::string& DiskImageSession::LastError() const { return last_error_; }

DiskImageSession::RuntimeStats DiskImageSession::GetRuntimeStats() const {
  RuntimeStats s;
  s.sector_cache.hits = sector_cache_.Hits();
  s.sector_cache.misses = sector_cache_.Misses();
  s.sector_cache.size = sector_cache_.Size();
  s.sector_cache.capacity = sector_cache_.Capacity();
  s.sector_cache.hit_rate = sector_cache_.HitRate();

  s.file_cache.hits = file_cache_.Hits();
  s.file_cache.misses = file_cache_.Misses();
  s.file_cache.size = file_cache_.Size();
  s.file_cache.capacity = file_cache_.Capacity();
  s.file_cache.hit_rate = file_cache_.HitRate();

  s.bytes_served = bytes_served_;
  s.read_ops = read_ops_;
  s.open_count = open_count_;
  s.file_cache_max_item_size = file_cache_max_item_size_;

  if (read_ops_ > 0) {
    s.avg_read_latency_us = static_cast<double>(total_read_latency_us_) / static_cast<double>(read_ops_);
  }
  if (has_first_read_time_) {
    const auto elapsed_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                               first_read_time_)
            .count());
    if (elapsed_us > 0) {
      s.throughput_bytes_per_sec =
          static_cast<double>(bytes_served_) * 1000000.0 / static_cast<double>(elapsed_us);
    }
  }
  return s;
}

void DiskImageSession::ResetRuntimeStats() {
  sector_cache_.ResetStats();
  file_cache_.ResetStats();
  bytes_served_ = 0;
  read_ops_ = 0;
  open_count_ = 0;
  total_read_latency_us_ = 0;
  has_first_read_time_ = false;
}

}  // namespace jdrive64
