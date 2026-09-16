#include "jdrive64/disk_image_session.hpp"

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

const BAMReader& DiskImageSession::Bam() const { return bam_; }

const DiskCatalog& DiskImageSession::Catalog() const { return catalog_; }

bool DiskImageSession::ReadFileByWindowsName(const std::string& windows_name,
                                             std::vector<std::uint8_t>* data) {
  last_error_.clear();
  if (data == nullptr) {
    last_error_ = "Invalid output buffer";
    return false;
  }

  if (file_cache_.Get(windows_name, data)) {
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
  if (data == nullptr) {
    last_error_ = "Invalid output buffer";
    return false;
  }

  const std::string windows_name = file.windows_name;
  if (file_cache_.Get(windows_name, data)) {
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

  file_cache_.Put(windows_name, bytes);
  *data = std::move(bytes);
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
  return s;
}

void DiskImageSession::ResetRuntimeStats() {
  sector_cache_.ResetStats();
  file_cache_.ResetStats();
  bytes_served_ = 0;
  read_ops_ = 0;
  open_count_ = 0;
}

}  // namespace jdrive64
