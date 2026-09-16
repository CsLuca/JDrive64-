#include "jdrive64/winfsp_filesystem.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>

namespace jdrive64 {

bool WinFspFilesystem::MountReadOnly(const std::string& image_path, const std::string& mount_point) {
  last_error_.clear();

  if (mounted_) {
    last_error_ = "Already mounted";
    return false;
  }

  image_path_ = image_path;
  mount_point_ = mount_point;
  std::transform(mount_point_.begin(), mount_point_.end(), mount_point_.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  if (mount_point_.size() != 2 || mount_point_[1] != ':') {
    last_error_ = "Invalid mount point, expected format X:";
    return false;
  }

  if (!LoadImageSession()) {
    return false;
  }

  mounted_ = true;
  return true;
}

bool WinFspFilesystem::Unmount(const std::string& mount_point) {
  last_error_.clear();

  if (!mounted_) {
    last_error_ = "Not mounted";
    return false;
  }

  std::string normalized = mount_point;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });

  if (normalized != mount_point_) {
    last_error_ = "Mount point mismatch";
    return false;
  }

  mounted_ = false;
  image_path_.clear();
  mount_point_.clear();
  return true;
}

bool WinFspFilesystem::IsMounted() const { return mounted_; }

const std::string& WinFspFilesystem::LastError() const { return last_error_; }

std::string WinFspFilesystem::GetVolumeInfoText() const {
  std::ostringstream oss;
  oss << "Label: " << session_.Bam().DiskName() << "\n"
      << "File System: JDrive64\n"
      << "Capacity: 664 Blocks\n"
      << "Free: " << session_.Bam().FreeBlocks() << " Blocks";
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
  last_error_.clear();
  if (!mounted_) {
    last_error_ = "Not mounted";
    return false;
  }
  if (data == nullptr) {
    last_error_ = "Invalid output buffer";
    return false;
  }

  if (!session_.ReadFileByWindowsName(windows_name, data)) {
    last_error_ = session_.LastError();
    return false;
  }

  return true;
}

bool WinFspFilesystem::WriteFileByWindowsName(const std::string& windows_name,
                                              const std::vector<std::uint8_t>& data) {
  (void)windows_name;
  (void)data;
  last_error_ = "ACCESS_DENIED";
  return false;
}

bool WinFspFilesystem::DeleteByWindowsName(const std::string& windows_name) {
  (void)windows_name;
  last_error_ = "ACCESS_DENIED";
  return false;
}

bool WinFspFilesystem::RenameByWindowsName(const std::string& old_name,
                                           const std::string& new_name) {
  (void)old_name;
  (void)new_name;
  last_error_ = "ACCESS_DENIED";
  return false;
}

bool WinFspFilesystem::GetVolumeInfo(VolumeInfo* info) {
  last_error_.clear();
  if (!mounted_) {
    last_error_ = "Not mounted";
    return false;
  }
  if (info == nullptr) {
    last_error_ = "Invalid output buffer";
    return false;
  }

  info->label = session_.Bam().DiskName();
  info->filesystem = "JDrive64";
  info->capacity_blocks = 664;
  info->free_blocks = session_.Bam().FreeBlocks();
  return true;
}

bool WinFspFilesystem::GetFileInfo(const std::string& windows_name, FileInfo* info) {
  last_error_.clear();
  if (!mounted_) {
    last_error_ = "Not mounted";
    return false;
  }
  if (info == nullptr) {
    last_error_ = "Invalid output buffer";
    return false;
  }

  if (windows_name.empty() || windows_name == "\\" || windows_name == "/") {
    info->windows_name = "\\";
    info->is_directory = true;
    info->size_bytes = 0;
    return true;
  }

  const auto* file = session_.Catalog().FindByWindowsName(windows_name);
  if (file == nullptr) {
    last_error_ = "File not found";
    return false;
  }

  std::vector<std::uint8_t> bytes;
  if (!session_.ReadFileByCatalogFile(*file, &bytes)) {
    last_error_ = session_.LastError();
    return false;
  }

  info->windows_name = file->windows_name;
  info->is_directory = false;
  info->size_bytes = bytes.size();
  return true;
}

bool WinFspFilesystem::Open(const std::string& windows_name, std::uint64_t* handle_out) {
  last_error_.clear();
  if (!mounted_) {
    last_error_ = "Not mounted";
    return false;
  }
  if (handle_out == nullptr) {
    last_error_ = "Invalid output buffer";
    return false;
  }

  const auto* file = session_.Catalog().FindByWindowsName(windows_name);
  if (file == nullptr) {
    last_error_ = "File not found";
    return false;
  }

  open_handles_.push_back(file->windows_name);
  *handle_out = static_cast<std::uint64_t>(open_handles_.size());
  return true;
}

bool WinFspFilesystem::Read(std::uint64_t handle,
                            std::uint64_t offset,
                            std::uint32_t size,
                            std::vector<std::uint8_t>* out_bytes) {
  last_error_.clear();
  if (!mounted_) {
    last_error_ = "Not mounted";
    return false;
  }
  if (out_bytes == nullptr) {
    last_error_ = "Invalid output buffer";
    return false;
  }
  if (!IsValidHandle(handle)) {
    last_error_ = "Invalid handle";
    return false;
  }

  const std::string& windows_name = open_handles_[static_cast<std::size_t>(handle - 1)];
  std::vector<std::uint8_t> file_bytes;
  if (!session_.ReadFileByWindowsName(windows_name, &file_bytes)) {
    last_error_ = session_.LastError();
    return false;
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
  last_error_.clear();
  if (!mounted_) {
    last_error_ = "Not mounted";
    return false;
  }
  if (!IsValidHandle(handle)) {
    last_error_ = "Invalid handle";
    return false;
  }

  open_handles_[static_cast<std::size_t>(handle - 1)].clear();
  return true;
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
    last_error_ = session_.LastError();
    return false;
  }

  return true;
}

}  // namespace jdrive64
