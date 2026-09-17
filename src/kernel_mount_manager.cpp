#include "jdrive64/kernel_mount_manager.hpp"

namespace jdrive64 {

bool KernelMountManager::AssignDriveLetter(const std::string& mount_point) {
  if (mount_point.size() != 2 || mount_point[1] != ':') {
    last_error_ = "Invalid mount point";
    return false;
  }
  if (assigned_) {
    last_error_ = "Drive letter already assigned";
    return false;
  }

  assigned_ = true;
  assigned_mount_point_ = mount_point;
  last_error_ = "Mount manager integration not implemented yet";
  return false;
}

bool KernelMountManager::ReleaseDriveLetter(const std::string& mount_point) {
  if (!assigned_) {
    last_error_ = "Drive letter is not assigned";
    return false;
  }
  if (mount_point != assigned_mount_point_) {
    last_error_ = "Assigned drive letter mismatch";
    return false;
  }

  assigned_ = false;
  assigned_mount_point_.clear();
  last_error_.clear();
  return true;
}

bool KernelMountManager::IsAssigned() const { return assigned_; }

const std::string& KernelMountManager::AssignedMountPoint() const { return assigned_mount_point_; }

const std::string& KernelMountManager::LastError() const { return last_error_; }

}  // namespace jdrive64
