#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace jdrive64 {

class D64Reader;
struct DirectoryEntry;

class FileChainReader {
 public:
  explicit FileChainReader(D64Reader& reader);

  std::vector<std::uint8_t> ReadFile(const DirectoryEntry& entry);
  const std::string& LastError() const;

 private:
  D64Reader& reader_;
  std::string last_error_;
};

}  // namespace jdrive64
