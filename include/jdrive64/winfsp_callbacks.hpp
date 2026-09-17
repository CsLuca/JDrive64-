#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "jdrive64/winfsp_filesystem.hpp"

namespace jdrive64 {

class WinFspCallbacks {
 public:
  struct CallbackTable {
    using GetVolumeInfoFn = bool (*)(void* user_data, WinFspFilesystem::VolumeInfo* info);
    using ReadDirectoryFn = bool (*)(void* user_data, std::vector<std::string>* entries);
    using OpenFn = bool (*)(void* user_data, const std::string& windows_name, std::uint64_t* handle_out);
    using ReadFn = bool (*)(void* user_data,
                            std::uint64_t handle,
                            std::uint64_t offset,
                            std::uint32_t size,
                            std::vector<std::uint8_t>* out_bytes);
    using CloseFn = bool (*)(void* user_data, std::uint64_t handle);

    GetVolumeInfoFn get_volume_info = nullptr;
    ReadDirectoryFn read_directory = nullptr;
    OpenFn open = nullptr;
    ReadFn read = nullptr;
    CloseFn close = nullptr;
  };

  bool Initialize(WinFspFilesystem* filesystem);
  void Shutdown();

  bool IsInitialized() const;
  const std::string& LastError() const;

  const CallbackTable& Table() const;
  void* UserData() const;

  bool DispatchGetVolumeInfo(WinFspFilesystem::VolumeInfo* info) const;
  bool DispatchReadDirectory(std::vector<std::string>* entries) const;
  bool DispatchOpen(const std::string& windows_name, std::uint64_t* handle_out) const;
  bool DispatchRead(std::uint64_t handle,
                    std::uint64_t offset,
                    std::uint32_t size,
                    std::vector<std::uint8_t>* out_bytes) const;
  bool DispatchClose(std::uint64_t handle) const;

 private:
  static bool TrampolineGetVolumeInfo(void* user_data, WinFspFilesystem::VolumeInfo* info);
  static bool TrampolineReadDirectory(void* user_data, std::vector<std::string>* entries);
  static bool TrampolineOpen(void* user_data, const std::string& windows_name, std::uint64_t* handle_out);
  static bool TrampolineRead(void* user_data,
                             std::uint64_t handle,
                             std::uint64_t offset,
                             std::uint32_t size,
                             std::vector<std::uint8_t>* out_bytes);
  static bool TrampolineClose(void* user_data, std::uint64_t handle);

  bool initialized_ = false;
  std::string last_error_;
  WinFspFilesystem* filesystem_ = nullptr;
  CallbackTable table_{};
};

}  // namespace jdrive64
