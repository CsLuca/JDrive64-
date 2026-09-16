#include "jdrive64/winfsp_filesystem.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>

namespace jdrive64 {

namespace {

constexpr std::uint32_t kErrorSuccess = 0;
constexpr std::uint32_t kErrorAccessDenied = 5;
constexpr std::uint32_t kErrorFileNotFound = 2;
constexpr std::uint32_t kErrorInvalidParameter = 87;
constexpr std::uint32_t kErrorNotReady = 21;
constexpr std::uint32_t kErrorAlreadyExists = 183;
constexpr std::uint32_t kErrorNotSupported = 50;
constexpr std::uint32_t kErrorInvalidHandle = 6;
constexpr std::uint32_t kErrorBadCommand = 22;

constexpr std::uint32_t kNtStatusSuccess = 0x00000000;
constexpr std::uint32_t kNtStatusAccessDenied = 0xC0000022;
constexpr std::uint32_t kNtStatusObjectNameNotFound = 0xC0000034;
constexpr std::uint32_t kNtStatusInvalidParameter = 0xC000000D;
constexpr std::uint32_t kNtStatusDeviceNotReady = 0xC00000A3;
constexpr std::uint32_t kNtStatusObjectNameCollision = 0xC0000035;
constexpr std::uint32_t kNtStatusNotSupported = 0xC00000BB;
constexpr std::uint32_t kNtStatusInvalidHandle = 0xC0000008;
constexpr std::uint32_t kNtStatusUnsuccessful = 0xC0000001;
constexpr std::uint32_t kD64BlockSizeBytes = 256;
constexpr std::uint32_t kD64TotalBlocks = 664;

}  // namespace

bool WinFspFilesystem::MountReadOnly(const std::string& image_path, const std::string& mount_point) {
  SetSuccess();

  if (mounted_) {
    return SetError(FsStatus::kAlreadyMounted, "Already mounted");
  }

  image_path_ = image_path;
  mount_point_ = mount_point;
  std::transform(mount_point_.begin(), mount_point_.end(), mount_point_.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  if (mount_point_.size() != 2 || mount_point_[1] != ':') {
    return SetError(FsStatus::kInvalidMountPoint, "Invalid mount point, expected format X:");
  }

  if (!LoadImageSession()) {
    return false;
  }

  mounted_ = true;
  return true;
}

bool WinFspFilesystem::Unmount(const std::string& mount_point) {
  SetSuccess();

  if (!mounted_) {
    return SetError(FsStatus::kNotMounted, "Not mounted");
  }

  std::string normalized = mount_point;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });

  if (normalized != mount_point_) {
    return SetError(FsStatus::kMountPointMismatch, "Mount point mismatch");
  }

  mounted_ = false;
  image_path_.clear();
  mount_point_.clear();
  return true;
}

bool WinFspFilesystem::IsMounted() const { return mounted_; }

const std::string& WinFspFilesystem::LastError() const { return last_error_; }

WinFspFilesystem::FsStatus WinFspFilesystem::LastStatus() const { return last_status_; }

std::uint32_t WinFspFilesystem::LastWin32Error() const { return StatusToWin32(last_status_); }

std::uint32_t WinFspFilesystem::LastNtStatus() const { return StatusToNtStatus(last_status_); }

std::string WinFspFilesystem::GetVolumeInfoText() const {
  const std::uint32_t free_blocks = session_.Bam().FreeBlocks();
  const std::uint32_t used_blocks = kD64TotalBlocks - free_blocks;
  const std::uint64_t capacity_bytes =
      static_cast<std::uint64_t>(kD64TotalBlocks) * kD64BlockSizeBytes;
  const std::uint64_t free_bytes = static_cast<std::uint64_t>(free_blocks) * kD64BlockSizeBytes;
  const std::uint64_t used_bytes = static_cast<std::uint64_t>(used_blocks) * kD64BlockSizeBytes;

  std::ostringstream oss;
  oss << "Label: " << session_.Bam().DiskName() << "\n"
      << "File System: JDrive64\n"
      << "Block Size: " << kD64BlockSizeBytes << " Bytes\n"
      << "Capacity: " << kD64TotalBlocks << " Blocks (" << capacity_bytes << " Bytes)\n"
      << "Used: " << used_blocks << " Blocks (" << used_bytes << " Bytes)\n"
      << "Free: " << free_blocks << " Blocks (" << free_bytes << " Bytes)";
  return oss.str();
}

std::vector<std::string> WinFspFilesystem::ReadDirectory() const {
  std::vector<std::string> out;
  out.reserve(session_.Catalog().Files().size());
  for (const auto& file : session_.Catalog().Files()) {
    out.push_back(file.windows_name);
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool WinFspFilesystem::ReadFileByWindowsName(const std::string& windows_name,
                                             std::vector<std::uint8_t>* data) {
  SetSuccess();
  if (!mounted_) {
    return SetError(FsStatus::kNotMounted, "Not mounted");
  }
  if (data == nullptr) {
    return SetError(FsStatus::kInvalidParameter, "Invalid output buffer");
  }

  if (!session_.ReadFileByWindowsName(windows_name, data)) {
    if (session_.LastError() == "File not found") {
      return SetError(FsStatus::kFileNotFound, session_.LastError());
    }
    return SetError(FsStatus::kIoError, session_.LastError());
  }

  return true;
}

bool WinFspFilesystem::CreateByWindowsName(const std::string& windows_name) {
  (void)windows_name;
  return SetError(FsStatus::kAccessDenied, "ACCESS_DENIED");
}

bool WinFspFilesystem::WriteFileByWindowsName(const std::string& windows_name,
                                              const std::vector<std::uint8_t>& data) {
  (void)windows_name;
  (void)data;
  return SetError(FsStatus::kAccessDenied, "ACCESS_DENIED");
}

bool WinFspFilesystem::SetFileSizeByWindowsName(const std::string& windows_name,
                                                std::uint64_t size_bytes) {
  (void)windows_name;
  (void)size_bytes;
  return SetError(FsStatus::kAccessDenied, "ACCESS_DENIED");
}

bool WinFspFilesystem::SetFileAttributesByWindowsName(const std::string& windows_name,
                                                      std::uint32_t attributes_mask) {
  (void)windows_name;
  (void)attributes_mask;
  return SetError(FsStatus::kAccessDenied, "ACCESS_DENIED");
}

bool WinFspFilesystem::DeleteByWindowsName(const std::string& windows_name) {
  (void)windows_name;
  return SetError(FsStatus::kAccessDenied, "ACCESS_DENIED");
}

bool WinFspFilesystem::RenameByWindowsName(const std::string& old_name,
                                           const std::string& new_name) {
  (void)old_name;
  (void)new_name;
  return SetError(FsStatus::kAccessDenied, "ACCESS_DENIED");
}

bool WinFspFilesystem::GetVolumeInfo(VolumeInfo* info) {
  SetSuccess();
  if (!mounted_) {
    return SetError(FsStatus::kNotMounted, "Not mounted");
  }
  if (info == nullptr) {
    return SetError(FsStatus::kInvalidParameter, "Invalid output buffer");
  }

  info->label = session_.Bam().DiskName();
  info->filesystem = "JDrive64";
  info->block_size_bytes = kD64BlockSizeBytes;
  info->capacity_blocks = kD64TotalBlocks;
  info->free_blocks = session_.Bam().FreeBlocks();
  info->used_blocks = info->capacity_blocks - info->free_blocks;
  info->capacity_bytes = static_cast<std::uint64_t>(info->capacity_blocks) * info->block_size_bytes;
  info->free_bytes = static_cast<std::uint64_t>(info->free_blocks) * info->block_size_bytes;
  info->used_bytes = static_cast<std::uint64_t>(info->used_blocks) * info->block_size_bytes;
  return true;
}

bool WinFspFilesystem::GetFileInfo(const std::string& windows_name, FileInfo* info) {
  SetSuccess();
  if (!mounted_) {
    return SetError(FsStatus::kNotMounted, "Not mounted");
  }
  if (info == nullptr) {
    return SetError(FsStatus::kInvalidParameter, "Invalid output buffer");
  }

  if (windows_name.empty() || windows_name == "\\" || windows_name == "/") {
    info->windows_name = "\\";
    info->is_directory = true;
    info->size_bytes = 0;
    return true;
  }

  const auto* file = session_.Catalog().FindByWindowsName(windows_name);
  if (file == nullptr) {
    return SetError(FsStatus::kFileNotFound, "File not found");
  }

  std::vector<std::uint8_t> bytes;
  if (!session_.ReadFileByCatalogFile(*file, &bytes)) {
    return SetError(FsStatus::kIoError, session_.LastError());
  }

  info->windows_name = file->windows_name;
  info->is_directory = false;
  info->size_bytes = bytes.size();
  return true;
}

bool WinFspFilesystem::Open(const std::string& windows_name, std::uint64_t* handle_out) {
  SetSuccess();
  if (!mounted_) {
    return SetError(FsStatus::kNotMounted, "Not mounted");
  }
  if (handle_out == nullptr) {
    return SetError(FsStatus::kInvalidParameter, "Invalid output buffer");
  }

  const auto* file = session_.Catalog().FindByWindowsName(windows_name);
  if (file == nullptr) {
    return SetError(FsStatus::kFileNotFound, "File not found");
  }

  open_handles_.push_back(file->windows_name);
  *handle_out = static_cast<std::uint64_t>(open_handles_.size());
  return true;
}

bool WinFspFilesystem::Read(std::uint64_t handle,
                            std::uint64_t offset,
                            std::uint32_t size,
                            std::vector<std::uint8_t>* out_bytes) {
  SetSuccess();
  if (!mounted_) {
    return SetError(FsStatus::kNotMounted, "Not mounted");
  }
  if (out_bytes == nullptr) {
    return SetError(FsStatus::kInvalidParameter, "Invalid output buffer");
  }
  if (!IsValidHandle(handle)) {
    return SetError(FsStatus::kInvalidHandle, "Invalid handle");
  }

  const std::string& windows_name = open_handles_[static_cast<std::size_t>(handle - 1)];
  std::vector<std::uint8_t> file_bytes;
  if (!session_.ReadFileByWindowsName(windows_name, &file_bytes)) {
    if (session_.LastError() == "File not found") {
      return SetError(FsStatus::kFileNotFound, session_.LastError());
    }
    return SetError(FsStatus::kIoError, session_.LastError());
  }

  out_bytes->clear();
  if (offset >= file_bytes.size() || size == 0) {
    return true;
  }

  const auto start = static_cast<std::size_t>(offset);
  const auto remaining = file_bytes.size() - start;
  const auto read_count = std::min<std::size_t>(remaining, size);
  out_bytes->insert(out_bytes->end(), file_bytes.begin() + static_cast<std::ptrdiff_t>(start),
                    file_bytes.begin() + static_cast<std::ptrdiff_t>(start + read_count));
  return true;
}

bool WinFspFilesystem::Close(std::uint64_t handle) {
  SetSuccess();
  if (!mounted_) {
    return SetError(FsStatus::kNotMounted, "Not mounted");
  }
  if (!IsValidHandle(handle)) {
    return SetError(FsStatus::kInvalidHandle, "Invalid handle");
  }

  open_handles_[static_cast<std::size_t>(handle - 1)].clear();
  return true;
}

WinFspFilesystem::RuntimeStats WinFspFilesystem::GetRuntimeStats() const {
  return session_.GetRuntimeStats();
}

std::string WinFspFilesystem::GetRuntimeStatsText() const {
  const auto s = session_.GetRuntimeStats();
  std::ostringstream oss;
  oss << "SectorCache hits=" << s.sector_cache.hits << " misses=" << s.sector_cache.misses
      << " size=" << s.sector_cache.size << "/" << s.sector_cache.capacity
      << " hitRate=" << s.sector_cache.hit_rate << "\n"
      << "FileCache hits=" << s.file_cache.hits << " misses=" << s.file_cache.misses
      << " size=" << s.file_cache.size << "/" << s.file_cache.capacity
      << " hitRate=" << s.file_cache.hit_rate << "\n"
      << "ReadOps=" << s.read_ops << " BytesServed=" << s.bytes_served
      << " OpenCount=" << s.open_count << " AvgLatencyUs=" << s.avg_read_latency_us
      << " ThroughputBps=" << s.throughput_bytes_per_sec
      << " FileCacheMaxItem=" << s.file_cache_max_item_size;
  return oss.str();
}

void WinFspFilesystem::ConfigureCaches(std::size_t sector_cache_capacity,
                                       std::size_t file_cache_capacity,
                                       std::size_t file_cache_max_item_size) {
  session_.ConfigureCaches(sector_cache_capacity, file_cache_capacity, file_cache_max_item_size);
}

bool WinFspFilesystem::IsValidHandle(std::uint64_t handle) const {
  if (handle == 0) {
    return false;
  }

  const auto index = static_cast<std::size_t>(handle - 1);
  if (index >= open_handles_.size()) {
    return false;
  }

  return !open_handles_[index].empty();
}

bool WinFspFilesystem::LoadImageSession() {
  if (!session_.Open(image_path_)) {
    return SetError(FsStatus::kIoError, session_.LastError());
  }

  return true;
}

void WinFspFilesystem::SetSuccess() {
  last_status_ = FsStatus::kSuccess;
  last_error_.clear();
}

bool WinFspFilesystem::SetError(FsStatus status, const std::string& message) {
  last_status_ = status;
  last_error_ = message;
  return false;
}

std::uint32_t WinFspFilesystem::StatusToWin32(FsStatus status) {
  switch (status) {
    case FsStatus::kSuccess:
      return kErrorSuccess;
    case FsStatus::kAccessDenied:
      return kErrorAccessDenied;
    case FsStatus::kFileNotFound:
      return kErrorFileNotFound;
    case FsStatus::kInvalidParameter:
    case FsStatus::kInvalidMountPoint:
    case FsStatus::kMountPointMismatch:
      return kErrorInvalidParameter;
    case FsStatus::kNotMounted:
      return kErrorNotReady;
    case FsStatus::kAlreadyMounted:
      return kErrorAlreadyExists;
    case FsStatus::kInvalidHandle:
      return kErrorInvalidHandle;
    case FsStatus::kNotSupported:
      return kErrorNotSupported;
    case FsStatus::kIoError:
    case FsStatus::kInvalidState:
    default:
      return kErrorBadCommand;
  }
}

std::uint32_t WinFspFilesystem::StatusToNtStatus(FsStatus status) {
  switch (status) {
    case FsStatus::kSuccess:
      return kNtStatusSuccess;
    case FsStatus::kAccessDenied:
      return kNtStatusAccessDenied;
    case FsStatus::kFileNotFound:
      return kNtStatusObjectNameNotFound;
    case FsStatus::kInvalidParameter:
    case FsStatus::kInvalidMountPoint:
    case FsStatus::kMountPointMismatch:
      return kNtStatusInvalidParameter;
    case FsStatus::kNotMounted:
      return kNtStatusDeviceNotReady;
    case FsStatus::kAlreadyMounted:
      return kNtStatusObjectNameCollision;
    case FsStatus::kInvalidHandle:
      return kNtStatusInvalidHandle;
    case FsStatus::kNotSupported:
      return kNtStatusNotSupported;
    case FsStatus::kIoError:
    case FsStatus::kInvalidState:
    default:
      return kNtStatusUnsuccessful;
  }
}

}  // namespace jdrive64
