#pragma once

#include <string>

namespace jdrive64 {

class KernelMountManager {
 public:
  bool AssignDriveLetter(const std::string& mount_point);
  bool ReleaseDriveLetter(const std::string& mount_point);

  bool IsAssigned() const;
  const std::string& AssignedMountPoint() const;
  const std::string& LastError() const;

 private:
  bool assigned_ = false;
  std::string assigned_mount_point_;
  std::string last_error_;
};

}  // namespace jdrive64
