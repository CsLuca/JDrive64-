#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace jdrive64 {

class D64Reader;

struct DirectoryEntry {
  std::string name;
  std::string extension;
  std::uint8_t file_type = 0;
  std::uint8_t start_track = 0;
  std::uint8_t start_sector = 0;
  std::uint16_t size_blocks = 0;
};

class DirectoryReader {
 public:
  bool Load(D64Reader& reader);
  const std::vector<DirectoryEntry>& Entries() const;
  const std::string& LastError() const;

  static std::string FileTypeToExtension(std::uint8_t file_type);

 private:
  std::vector<DirectoryEntry> entries_;
  std::string last_error_;
};

}  // namespace jdrive64
