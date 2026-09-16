#include "jdrive64/winfsp_filesystem.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "jdrive64/file_chain_reader.hpp"

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

  if (!LoadImageMetadata()) {
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
  oss << "Label: " << bam_.DiskName() << "\n"
      << "File System: JDrive64\n"
      << "Capacity: 664 Blocks\n"
      << "Free: " << bam_.FreeBlocks() << " Blocks";
  return oss.str();
}

std::vector<std::string> WinFspFilesystem::ReadDirectory() const {
  std::vector<std::string> out;
  out.reserve(catalog_.Files().size());
  for (const auto& file : catalog_.Files()) {
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

  if (file_cache_.Get(windows_name, data)) {
    return true;
  }

  const auto* file = catalog_.FindByWindowsName(windows_name);
  if (file == nullptr) {
    last_error_ = "File not found";
    return false;
  }

  FileChainReader reader(reader_);
  auto bytes = reader.ReadFile(*file);
  if (!reader.LastError().empty()) {
    last_error_ = reader.LastError();
    return false;
  }

  file_cache_.Put(windows_name, bytes);
  *data = std::move(bytes);
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

bool WinFspFilesystem::LoadImageMetadata() {
  reader_.SetSectorCache(&sector_cache_);

  if (!reader_.Open(image_path_)) {
    last_error_ = reader_.LastError();
    return false;
  }

  if (!bam_.Load(reader_)) {
    last_error_ = bam_.LastError();
    return false;
  }

  if (!catalog_.Build(reader_)) {
    last_error_ = catalog_.LastError();
    return false;
  }

  return true;
}

}  // namespace jdrive64
