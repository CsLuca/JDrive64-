#include "jdrive64/disk_catalog.hpp"

#include <algorithm>
#include <cctype>

#include "jdrive64/d64_reader.hpp"
#include "jdrive64/directory_reader.hpp"

namespace jdrive64 {

namespace {

std::string SanitizeWindowsFilename(std::string value) {
  static const std::string invalid = "<>:\\|?*\"/";
  for (char& c : value) {
    if (static_cast<unsigned char>(c) < 32 || invalid.find(c) != std::string::npos) {
      c = '_';
    }
  }
  if (value.empty()) {
    value = "UNNAMED";
  }
  return value;
}

}  // namespace

bool DiskCatalog::Build(D64Reader& reader) {
  files_.clear();
  by_windows_name_.clear();
  last_error_.clear();

  DirectoryReader directory_reader;
  if (!directory_reader.Load(reader)) {
    last_error_ = directory_reader.LastError();
    return false;
  }

  for (const DirectoryEntry& entry : directory_reader.Entries()) {
    CatalogFile file;
    file.display_name = entry.name;
    file.extension = entry.extension;
    file.file_type = entry.file_type;
    file.start_track = entry.start_track;
    file.start_sector = entry.start_sector;
    file.size_blocks = entry.size_blocks;
    file.windows_name = SanitizeWindowsFilename(entry.name) + "." + entry.extension;

    const std::size_t index = files_.size();
    files_.push_back(file);
    by_windows_name_[ToUpper(file.windows_name)] = index;
  }

  return true;
}

const std::vector<CatalogFile>& DiskCatalog::Files() const { return files_; }

const CatalogFile* DiskCatalog::FindByWindowsName(const std::string& windows_name) const {
  const auto key = ToUpper(windows_name);
  const auto it = by_windows_name_.find(key);
  if (it == by_windows_name_.end()) {
    return nullptr;
  }
  return &files_[it->second];
}

const std::string& DiskCatalog::LastError() const { return last_error_; }

std::string DiskCatalog::ToUpper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return value;
}

}  // namespace jdrive64
