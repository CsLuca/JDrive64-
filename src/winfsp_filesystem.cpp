#include "jdrive64/winfsp_filesystem.hpp"

#include <algorithm>
#include <cctype>
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

bool WinFspFilesystem::LoadImageSession() {
  if (!session_.Open(image_path_)) {
    last_error_ = session_.LastError();
    return false;
  }

  return true;
}

}  // namespace jdrive64
