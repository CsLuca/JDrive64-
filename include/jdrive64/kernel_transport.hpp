#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "jdrive64/kernel_ioctl_protocol.hpp"
#include "jdrive64/kernel_user_bridge.hpp"

namespace jdrive64 {

class KernelTransport {
 public:
  enum class Mode {
    kLoopback = 0,
    kDevice,
  };

  bool SetMode(Mode mode);
  Mode GetMode() const;

  bool Connect(const std::string& image_path);
  bool Disconnect();
  bool Send(const KernelRequest& request, KernelResponse* response);

  bool BuildDeviceFrame(const KernelRequest& request, std::vector<std::uint8_t>* frame) const;

  bool IsConnected() const;
  const std::string& LastError() const;

 private:
  Mode mode_ = Mode::kLoopback;
  bool connected_ = false;
  void* device_handle_ = nullptr;
  KernelUserBridge bridge_;
  std::string last_error_;
};

}  // namespace jdrive64
