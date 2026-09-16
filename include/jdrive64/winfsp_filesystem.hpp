#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "jdrive64/disk_image_session.hpp"

namespace jdrive64 {

class WinFspFilesystem {
 public:
  enum class FsStatus {
    kSuccess = 0,
    kAccessDenied,
    kNotMounted,
    kAlreadyMounted,
    kInvalidMountPoint,
    kMountPointMismatch,
    kInvalidParameter,
    kFileNotFound,
    kInvalidHandle,
    kIoError,
    kInvalidState,
    kNotSupported,
  };

  struct VolumeInfo {
    std::string label;
    std::string filesystem;
    std::uint32_t capacity_blocks = 664;
    std::uint32_t free_blocks = 0;
  };

  struct FileInfo {
    std::string windows_name;
    std::uint64_t size_bytes = 0;
    bool is_directory = false;
  };

  using RuntimeStats = DiskImageSession::RuntimeStats;

  bool MountReadOnly(const std::string& image_path, const std::string& mount_point);
  bool Unmount(const std::string& mount_point);

  bool IsMounted() const;
  const std::string& LastError() const;
  FsStatus LastStatus() const;
  std::uint32_t LastWin32Error() const;
  std::uint32_t LastNtStatus() const;

  std::string GetVolumeInfoText() const;
  std::vector<std::string> ReadDirectory() const;
  bool ReadFileByWindowsName(const std::string& windows_name, std::vector<std::uint8_t>* data);
  bool CreateByWindowsName(const std::string& windows_name);
  bool WriteFileByWindowsName(const std::string& windows_name, const std::vector<std::uint8_t>& data);
  bool DeleteByWindowsName(const std::string& windows_name);
  bool RenameByWindowsName(const std::string& old_name, const std::string& new_name);
  bool SetFileSizeByWindowsName(const std::string& windows_name, std::uint64_t size_bytes);
  bool SetFileAttributesByWindowsName(const std::string& windows_name, std::uint32_t attributes_mask);

  bool GetVolumeInfo(VolumeInfo* info);
  bool GetFileInfo(const std::string& windows_name, FileInfo* info);
  bool Open(const std::string& windows_name, std::uint64_t* handle_out);
  bool Read(std::uint64_t handle,
            std::uint64_t offset,
            std::uint32_t size,
            std::vector<std::uint8_t>* out_bytes);
  bool Close(std::uint64_t handle);

  RuntimeStats GetRuntimeStats() const;
  std::string GetRuntimeStatsText() const;
  void ConfigureCaches(std::size_t sector_cache_capacity,
                       std::size_t file_cache_capacity,
                       std::size_t file_cache_max_item_size);

 private:
  void SetSuccess();
  bool SetError(FsStatus status, const std::string& message);
  static std::uint32_t StatusToWin32(FsStatus status);
  static std::uint32_t StatusToNtStatus(FsStatus status);

  bool IsValidHandle(std::uint64_t handle) const;

  bool LoadImageSession();

  std::string image_path_;
  std::string mount_point_;
  bool mounted_ = false;
  FsStatus last_status_ = FsStatus::kSuccess;
  std::string last_error_;

  DiskImageSession session_;
  std::vector<std::string> open_handles_;
};

}  // namespace jdrive64
