#pragma once

#include <string>

#include "jdrive64/kernel_ioctl_protocol.hpp"
#include "jdrive64/kernel_user_bridge.hpp"

namespace jdrive64 {

class KernelIpcChannel {
 public:
  bool Connect(const std::string& image_path);
  bool Disconnect();

  bool Send(const KernelRequest& request, KernelResponse* response);

  bool IsConnected() const;
  const std::string& LastError() const;

 private:
  bool connected_ = false;
  KernelUserBridge bridge_;
  std::string last_error_;
};

}  // namespace jdrive64
