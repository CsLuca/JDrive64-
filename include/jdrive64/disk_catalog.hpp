#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace jdrive64 {

class D64Reader;

struct CatalogFile {
  std::string display_name;
  std::string windows_name;
  std::string extension;
  std::uint8_t file_type = 0;
  std::uint8_t start_track = 0;
  std::uint8_t start_sector = 0;
  std::uint16_t size_blocks = 0;
};

class DiskCatalog {
 public:
  bool Build(D64Reader& reader);

  const std::vector<CatalogFile>& Files() const;
  const CatalogFile* FindByWindowsName(const std::string& windows_name) const;
  const std::string& LastError() const;

 private:
  static std::string ToUpper(std::string value);

  std::vector<CatalogFile> files_;
  std::unordered_map<std::string, std::size_t> by_windows_name_;
  std::string last_error_;
};

}  // namespace jdrive64
