#pragma once

#include <string>

#include "jdrive64/winfsp_callbacks.hpp"

namespace jdrive64 {

class WinFspNativeApi {
 public:
  virtual ~WinFspNativeApi() = default;

  virtual bool RegisterReadOnly(const std::string& mount_point,
                                const WinFspCallbacks& callbacks,
                                std::string* error_out) = 0;
  virtual bool Unregister(std::string* error_out) = 0;
};

class DefaultWinFspNativeApi final : public WinFspNativeApi {
 public:
  ~DefaultWinFspNativeApi() override;

  bool RegisterReadOnly(const std::string& mount_point,
                        const WinFspCallbacks& callbacks,
                        std::string* error_out) override;
  bool Unregister(std::string* error_out) override;

 private:
  bool EnsureBootstrapReady(std::string* error_out);

  void* proc_create_ = nullptr;
  void* proc_set_mount_point_ = nullptr;
  void* proc_start_dispatcher_ = nullptr;
  void* proc_stop_dispatcher_ = nullptr;
  void* proc_delete_ = nullptr;

  void* module_handle_ = nullptr;
  void* file_system_handle_ = nullptr;
  bool dispatcher_started_ = false;
  bool registration_active_ = false;
};

}  // namespace jdrive64
