#include "jdrive64/winfsp_native_bridge.hpp"

namespace jdrive64 {

WinFspNativeBridge::WinFspNativeBridge() : api_(&default_api_) {}

void WinFspNativeBridge::SetApi(WinFspNativeApi* api) { api_ = (api != nullptr) ? api : &default_api_; }

bool WinFspNativeBridge::RegisterReadOnly(const std::string& mount_point,
                                          const WinFspCallbacks& callbacks) {
  last_error_.clear();

  if (registered_) {
    last_error_ = "Native bridge already registered";
    return false;
  }
  if (!callbacks.IsInitialized()) {
    last_error_ = "Callback table is not initialized";
    return false;
  }
  if (callbacks.Table().get_volume_info == nullptr || callbacks.Table().read_directory == nullptr ||
      callbacks.Table().open == nullptr || callbacks.Table().read == nullptr ||
      callbacks.Table().close == nullptr || callbacks.UserData() == nullptr) {
    last_error_ = "Callback table is incomplete";
    return false;
  }

  if (api_ == nullptr) {
    last_error_ = "Native API provider is not configured";
    return false;
  }

  if (!api_->RegisterReadOnly(mount_point, callbacks, &last_error_)) {
    if (last_error_.empty()) {
      last_error_ = "Native API registration failed";
    }
    return false;
  }

  mount_point_ = mount_point;
  registered_ = true;
  return true;
}

bool WinFspNativeBridge::Unregister() {
  last_error_.clear();
  if (!registered_) {
    last_error_ = "Native bridge is not registered";
    return false;
  }
  if (api_ == nullptr) {
    last_error_ = "Native API provider is not configured";
    return false;
  }

  if (!api_->Unregister(&last_error_)) {
    if (last_error_.empty()) {
      last_error_ = "Native API unregister failed";
    }
    return false;
  }

  registered_ = false;
  mount_point_.clear();
  return true;
}

bool WinFspNativeBridge::IsRegistered() const { return registered_; }

const std::string& WinFspNativeBridge::LastError() const { return last_error_; }

const std::string& WinFspNativeBridge::RegisteredMountPoint() const { return mount_point_; }

}  // namespace jdrive64
