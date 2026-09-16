#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "jdrive64/disk_image_session.hpp"

namespace jdrive64 {

class WinFspFilesystem {
 public:
  bool MountReadOnly(const std::string& image_path, const std::string& mount_point);
  bool Unmount(const std::string& mount_point);

  bool IsMounted() const;
  const std::string& LastError() const;

  std::string GetVolumeInfoText() const;
  std::vector<std::string> ReadDirectory() const;
  bool ReadFileByWindowsName(const std::string& windows_name, std::vector<std::uint8_t>* data);
  bool WriteFileByWindowsName(const std::string& windows_name, const std::vector<std::uint8_t>& data);
  bool DeleteByWindowsName(const std::string& windows_name);
  bool RenameByWindowsName(const std::string& old_name, const std::string& new_name);

 private:
  bool LoadImageSession();

  std::string image_path_;
  std::string mount_point_;
  bool mounted_ = false;
  std::string last_error_;

  DiskImageSession session_;
};

}  // namespace jdrive64
