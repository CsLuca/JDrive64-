#pragma once

#include <cstdint>
#include <cstddef>
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
  bool ParseDeviceFrame(const std::vector<std::uint8_t>& frame, KernelResponse* response) const;

  static constexpr std::size_t kDeviceRequestHeaderSize = sizeof(std::uint32_t) + sizeof(std::uint64_t) +
                                                           sizeof(std::uint64_t) + sizeof(std::uint32_t) +
                                                           sizeof(std::uint32_t);
  static constexpr std::size_t kDeviceResponseHeaderSize = sizeof(std::uint32_t) + sizeof(std::uint32_t) +
                                                            sizeof(std::uint64_t) + sizeof(std::uint32_t);

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
