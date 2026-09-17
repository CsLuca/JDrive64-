#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace jdrive64 {

enum class KernelOpcode : std::uint32_t {
  kInvalid = 0,
  kReadDirectory = 1,
  kQueryFile = 2,
  kOpenFile = 3,
  kReadFile = 4,
  kCloseFile = 5,
};

struct KernelRequest {
  KernelOpcode opcode = KernelOpcode::kInvalid;
  std::string windows_name;
  std::uint64_t handle = 0;
  std::uint64_t offset = 0;
  std::uint32_t size = 0;
};

struct KernelResponse {
  bool success = false;
  std::string error;

  std::vector<std::string> directory_entries;

  std::string file_name;
  std::uint64_t file_size_bytes = 0;

  std::uint64_t handle = 0;
  std::vector<std::uint8_t> data;
};

}  // namespace jdrive64
