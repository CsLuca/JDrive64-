#pragma once

#include <string>

#include "jdrive64/winfsp_native_api.hpp"
#include "jdrive64/winfsp_callbacks.hpp"

namespace jdrive64 {

class WinFspNativeBridge {
 public:
  WinFspNativeBridge();

  void SetApi(WinFspNativeApi* api);

  bool RegisterReadOnly(const std::string& mount_point, const WinFspCallbacks& callbacks);
  bool Unregister();

  bool IsRegistered() const;
  const std::string& LastError() const;
  const std::string& RegisteredMountPoint() const;

 private:
  bool registered_ = false;
  std::string mount_point_;
  std::string last_error_;
  DefaultWinFspNativeApi default_api_;
  WinFspNativeApi* api_ = nullptr;
};

}  // namespace jdrive64
