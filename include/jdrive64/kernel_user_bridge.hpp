#pragma once

#include <string>

#include "jdrive64/kernel_ioctl_protocol.hpp"
#include "jdrive64/kernel_readonly_fs.hpp"

namespace jdrive64 {

class KernelUserBridge {
 public:
  bool Initialize(const std::string& image_path);
  bool Dispatch(const KernelRequest& request, KernelResponse* response);

  const std::string& LastError() const;

 private:
  bool initialized_ = false;
  KernelReadOnlyFilesystem filesystem_;
  std::string last_error_;
};

}  // namespace jdrive64
