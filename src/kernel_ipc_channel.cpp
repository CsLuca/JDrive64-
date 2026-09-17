#include "jdrive64/kernel_ipc_channel.hpp"

namespace jdrive64 {

bool KernelIpcChannel::Connect(const std::string& image_path) {
  if (connected_) {
    last_error_ = "IPC channel already connected";
    return false;
  }

  if (!bridge_.Initialize(image_path)) {
    last_error_ = bridge_.LastError();
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

  connected_ = false;
  last_error_.clear();
  return true;
}

bool KernelIpcChannel::Send(const KernelRequest& request, KernelResponse* response) {
  if (!connected_) {
    last_error_ = "IPC channel is not connected";
    return false;
  }

  if (!bridge_.Dispatch(request, response)) {
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

}  // namespace jdrive64
