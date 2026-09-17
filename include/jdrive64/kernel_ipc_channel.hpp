#pragma once

#include <cstdint>
#include <string>

#include "jdrive64/kernel_ioctl_protocol.hpp"
#include "jdrive64/kernel_transport.hpp"

namespace jdrive64 {

class KernelIpcChannel {
 public:
  bool Connect(const std::string& image_path);
  bool Disconnect();

  bool Send(const KernelRequest& request, KernelResponse* response);

  bool SetTransportMode(KernelTransport::Mode mode);
  KernelTransport::Mode GetTransportMode() const;
  KernelTransport::FeaturePolicy GetFeaturePolicy() const;
  bool IsHandshakeComplete() const;
  std::uint32_t NegotiatedProtocolVersion() const;
  std::uint32_t NegotiatedCapabilities() const;
  std::uint32_t NegotiatedFeatures() const;

  bool IsConnected() const;
  const std::string& LastError() const;

 private:
  bool connected_ = false;
  KernelTransport transport_;
  std::string last_error_;
};

}  // namespace jdrive64
