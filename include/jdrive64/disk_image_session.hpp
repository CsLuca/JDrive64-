#pragma once

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
  bool Open(const std::string& image_path);

  const BAMReader& Bam() const;
  const DiskCatalog& Catalog() const;

  bool ReadFileByWindowsName(const std::string& windows_name, std::vector<std::uint8_t>* data);
  bool ReadFileByCatalogFile(const CatalogFile& file, std::vector<std::uint8_t>* data);

  const std::string& LastError() const;

 private:
  D64Reader reader_;
  BAMReader bam_;
  DiskCatalog catalog_;
  SectorCache sector_cache_;
  FileCache file_cache_;
  std::string last_error_;
};

}  // namespace jdrive64
