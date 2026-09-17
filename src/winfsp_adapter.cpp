#include "jdrive64/winfsp_adapter.hpp"

namespace jdrive64 {

bool WinFspAdapter::StartReadOnly(WinFspFilesystem* filesystem,
                                  const std::string& image_path,
                                  const std::string& mount_point) {
  last_error_.clear();
  if (filesystem == nullptr) {
    last_error_ = "Invalid filesystem instance";
    return false;
  }

#if defined(JDRIVE64_ENABLE_WINFSP)
  if (!filesystem->MountReadOnly(image_path, mount_point)) {
    last_error_ = filesystem->LastError();
    return false;
  }

  if (!callbacks_.Initialize(filesystem)) {
    last_error_ = callbacks_.LastError();
    filesystem->Unmount(mount_point);
    return false;
  }

  if (!native_bridge_.RegisterReadOnly(mount_point, callbacks_)) {
    last_error_ = native_bridge_.LastError();
    callbacks_.Shutdown();
    filesystem->Unmount(mount_point);
    return false;
  }

  return true;
#else
  (void)image_path;
  (void)mount_point;
  last_error_ = "WinFsp adapter support is disabled (build with JDRIVE64_ENABLE_WINFSP)";
  return false;
#endif
}

bool WinFspAdapter::Stop(WinFspFilesystem* filesystem, const std::string& mount_point) {
  last_error_.clear();
  if (filesystem == nullptr) {
    last_error_ = "Invalid filesystem instance";
    return false;
  }

#if defined(JDRIVE64_ENABLE_WINFSP)
  if (!native_bridge_.Unregister()) {
    last_error_ = native_bridge_.LastError();
    return false;
  }

  callbacks_.Shutdown();

  if (!filesystem->Unmount(mount_point)) {
    last_error_ = filesystem->LastError();
    return false;
  }
  return true;
#else
  (void)mount_point;
  last_error_ = "WinFsp adapter support is disabled (build with JDRIVE64_ENABLE_WINFSP)";
  return false;
#endif
}

const std::string& WinFspAdapter::LastError() const { return last_error_; }

bool WinFspAdapter::IsCallbacksInitialized() const { return callbacks_.IsInitialized(); }

bool WinFspAdapter::IsNativeRegistered() const { return native_bridge_.IsRegistered(); }

const WinFspCallbacks& WinFspAdapter::Callbacks() const { return callbacks_; }

}  // namespace jdrive64
