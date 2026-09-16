#pragma once

#include <cstdint>
#include <string>

namespace jdrive64 {

class D64Reader;

class BAMReader {
 public:
  bool Load(D64Reader& reader);

  std::uint16_t FreeBlocks() const;
  const std::string& DiskName() const;
  const std::string& DiskId() const;
  const std::string& DosType() const;
  const std::string& LastError() const;

 private:
  std::uint16_t free_blocks_ = 0;
  std::string disk_name_;
  std::string disk_id_;
  std::string dos_type_;
  std::string last_error_;
};

}  // namespace jdrive64
