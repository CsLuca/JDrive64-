#include "jdrive64/disk_image_session.hpp"

#include "jdrive64/file_chain_reader.hpp"

namespace jdrive64 {

bool DiskImageSession::Open(const std::string& image_path) {
  last_error_.clear();

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
  return true;
}

const std::string& DiskImageSession::LastError() const { return last_error_; }

}  // namespace jdrive64
