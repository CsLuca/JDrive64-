#include "jdrive64/winfsp_runtime.hpp"

namespace jdrive64 {

bool WinFspRuntime::StartReadOnly(const std::string& image_path, const std::string& mount_point) {
  last_error_.clear();

  if (running_) {
    last_error_ = "Runtime already running";
    return false;
  }

#if defined(JDRIVE64_ENABLE_WINFSP)
  if (!filesystem_.MountReadOnly(image_path, mount_point)) {
    last_error_ = filesystem_.LastError();
    return false;
  }

  mount_point_ = mount_point;
  running_ = true;
  return true;
#else
  (void)image_path;
  (void)mount_point;
  last_error_ = "WinFsp runtime support is disabled (build with JDRIVE64_ENABLE_WINFSP)";
  return false;
#endif
}

bool WinFspRuntime::Stop() {
  last_error_.clear();

  if (!running_) {
    last_error_ = "Runtime is not running";
    return false;
  }

#if defined(JDRIVE64_ENABLE_WINFSP)
  if (!filesystem_.Unmount(mount_point_)) {
    last_error_ = filesystem_.LastError();
    return false;
  }

  mount_point_.clear();
  running_ = false;
  return true;
#else
  last_error_ = "WinFsp runtime support is disabled (build with JDRIVE64_ENABLE_WINFSP)";
  return false;
#endif
}

bool WinFspRuntime::IsRunning() const { return running_; }

const std::string& WinFspRuntime::LastError() const { return last_error_; }

}  // namespace jdrive64
