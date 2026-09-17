#include "jdrive64/kernel_ipc_channel.hpp"

namespace jdrive64 {

bool KernelIpcChannel::Connect(const std::string& image_path) {
  if (connected_) {
    last_error_ = "IPC channel already connected";
    return false;
  }

  if (!transport_.Connect(image_path)) {
    last_error_ = transport_.LastError();
    return false;
  }

  connected_ = true;
  last_error_.clear();
  return true;
}

bool KernelIpcChannel::Disconnect() {
  if (!connected_) {
    last_error_ = "IPC channel is not connected";
    return false;
  }

  if (!transport_.Disconnect()) {
    last_error_ = transport_.LastError();
    return false;
  }

  connected_ = false;
  last_error_.clear();
  return true;
}

bool KernelIpcChannel::Send(const KernelRequest& request, KernelResponse* response) {
  if (!connected_) {
    last_error_ = "IPC channel is not connected";
    return false;
  }

  if (!transport_.Send(request, response)) {
    if (response != nullptr && !response->error.empty()) {
      last_error_ = response->error;
    } else {
      last_error_ = "IPC dispatch failed";
    }
    return false;
  }

  last_error_.clear();
  return true;
}

bool KernelIpcChannel::IsConnected() const { return connected_; }

const std::string& KernelIpcChannel::LastError() const { return last_error_; }

bool KernelIpcChannel::SetTransportMode(KernelTransport::Mode mode) {
  if (!transport_.SetMode(mode)) {
    last_error_ = transport_.LastError();
    return false;
  }
  last_error_.clear();
  return true;
}

bool KernelIpcChannel::SetTelemetrySinkForTesting(KernelTransport::TelemetrySink* sink) {
  return transport_.SetTelemetrySinkForTesting(sink);
}

KernelTransport::Mode KernelIpcChannel::GetTransportMode() const { return transport_.GetMode(); }

KernelTransport::FeaturePolicy KernelIpcChannel::GetFeaturePolicy() const {
  return transport_.GetFeaturePolicy();
}

bool KernelIpcChannel::IsHandshakeComplete() const { return transport_.IsHandshakeComplete(); }

std::uint32_t KernelIpcChannel::NegotiatedProtocolVersion() const {
  return transport_.NegotiatedProtocolVersion();
}

std::uint32_t KernelIpcChannel::NegotiatedCapabilities() const {
  return transport_.NegotiatedCapabilities();
}

std::uint32_t KernelIpcChannel::NegotiatedFeatures() const {
  return transport_.NegotiatedFeatures();
}

bool KernelIpcChannel::HasTelemetrySink() const { return transport_.HasTelemetrySink(); }

}  // namespace jdrive64
