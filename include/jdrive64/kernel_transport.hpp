#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
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

  class DeviceIoApi {
   public:
    virtual ~DeviceIoApi() = default;
    virtual bool Open(const std::string& device_path, void** handle, std::string* error) = 0;
    virtual bool Close(void* handle, std::string* error) = 0;
    virtual bool Ioctl(void* handle,
                       const std::vector<std::uint8_t>& request_frame,
                       std::vector<std::uint8_t>* response_frame,
                       std::string* error) = 0;
  };

  bool SetDeviceIoApiForTesting(DeviceIoApi* api);

  bool Connect(const std::string& image_path);
  bool Disconnect();
  bool Send(const KernelRequest& request, KernelResponse* response);

  bool IsHandshakeComplete() const;
  std::uint32_t NegotiatedCapabilities() const;

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
  DeviceIoApi* ResolveDeviceIoApi();

  Mode mode_ = Mode::kLoopback;
  bool connected_ = false;
  void* device_handle_ = nullptr;
  std::unique_ptr<DeviceIoApi> default_device_io_api_;
  DeviceIoApi* device_io_api_ = nullptr;
  KernelUserBridge bridge_;
  bool handshake_complete_ = false;
  std::uint32_t negotiated_capabilities_ = 0;
  std::string last_error_;
};

}  // namespace jdrive64
