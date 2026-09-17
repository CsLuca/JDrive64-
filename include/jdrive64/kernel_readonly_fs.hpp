#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "jdrive64/disk_image_session.hpp"

namespace jdrive64 {

class KernelReadOnlyFilesystem {
 public:
  struct FileInfo {
    std::string windows_name;
    std::uint64_t size_bytes = 0;
  };

  bool OpenImage(const std::string& image_path);
  bool ReadDirectory(std::vector<std::string>* entries) const;
  bool QueryFile(const std::string& windows_name, FileInfo* info) const;

  bool OpenFile(const std::string& windows_name, std::uint64_t* handle_out);
  bool ReadFile(std::uint64_t handle,
                std::uint64_t offset,
                std::uint32_t size,
                std::vector<std::uint8_t>* out_bytes);
  bool CloseFile(std::uint64_t handle);

  const std::string& LastError() const;

 private:
  bool IsValidHandle(std::uint64_t handle) const;

  DiskImageSession session_;
  std::unordered_map<std::uint64_t, std::string> open_handles_;
  std::uint64_t next_handle_ = 1;
  std::string last_error_;
};

}  // namespace jdrive64
