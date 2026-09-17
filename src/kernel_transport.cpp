#include "jdrive64/kernel_transport.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <cstring>

namespace jdrive64 {

bool KernelTransport::SetMode(Mode mode) {
  if (connected_) {
    last_error_ = "Cannot change transport mode while connected";
    return false;
  }

  mode_ = mode;
  last_error_.clear();
  return true;
}

KernelTransport::Mode KernelTransport::GetMode() const { return mode_; }

bool KernelTransport::Connect(const std::string& image_path) {
  if (connected_) {
    last_error_ = "Transport already connected";
    return false;
  }

  if (mode_ == Mode::kLoopback) {
    if (!bridge_.Initialize(image_path)) {
      last_error_ = bridge_.LastError();
      return false;
    }
    connected_ = true;
    last_error_.clear();
    return true;
  }

#if defined(_WIN32)
  HANDLE h = CreateFileA("\\\\.\\JDrive64Kdrv",
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    last_error_ = "Kernel device channel not available";
    return false;
  }

  device_handle_ = h;
  connected_ = true;
  last_error_.clear();
  return true;
#else
  (void)image_path;
  last_error_ = "Device transport is only supported on Windows";
  return false;
#endif
}

bool KernelTransport::Disconnect() {
  if (!connected_) {
    last_error_ = "Transport is not connected";
    return false;
  }

  if (mode_ == Mode::kLoopback) {
    connected_ = false;
    last_error_.clear();
    return true;
  }

#if defined(_WIN32)
  if (device_handle_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(device_handle_));
    device_handle_ = nullptr;
  }
  connected_ = false;
  last_error_.clear();
  return true;
#else
  last_error_ = "Device transport is only supported on Windows";
  return false;
#endif
}

bool KernelTransport::Send(const KernelRequest& request, KernelResponse* response) {
  if (!connected_) {
    last_error_ = "Transport is not connected";
    return false;
  }

  if (mode_ == Mode::kLoopback) {
    if (!bridge_.Dispatch(request, response)) {
      if (response != nullptr && !response->error.empty()) {
        last_error_ = response->error;
      } else {
        last_error_ = "Loopback dispatch failed";
      }
      return false;
    }
    last_error_.clear();
    return true;
  }

  if (response != nullptr) {
    std::vector<std::uint8_t> frame;
    if (!BuildDeviceFrame(request, &frame)) {
      response->success = false;
      response->error = "Device request encoding failed";
      last_error_ = response->error;
      return false;
    }

    response->success = false;
    response->error = "Device transport request path not implemented";
  }
  last_error_ = "Device transport request path not implemented";
  return false;
}

bool KernelTransport::BuildDeviceFrame(const KernelRequest& request,
                                       std::vector<std::uint8_t>* frame) const {
  if (frame == nullptr) {
    return false;
  }

  constexpr std::size_t kOpcodeOffset = 0;
  constexpr std::size_t kHandleOffset = kOpcodeOffset + sizeof(std::uint32_t);
  constexpr std::size_t kOffsetOffset = kHandleOffset + sizeof(std::uint64_t);
  constexpr std::size_t kSizeOffset = kOffsetOffset + sizeof(std::uint64_t);
  constexpr std::size_t kPathBytesOffset = kSizeOffset + sizeof(std::uint32_t);
  constexpr std::size_t kHeaderSize = kPathBytesOffset + sizeof(std::uint32_t);

  const std::size_t path_size = request.windows_name.size();
  frame->assign(kHeaderSize + path_size, 0);

  const std::uint32_t opcode = static_cast<std::uint32_t>(request.opcode);
  const std::uint32_t path_bytes = static_cast<std::uint32_t>(path_size);
  std::memcpy(frame->data() + kOpcodeOffset, &opcode, sizeof(opcode));
  std::memcpy(frame->data() + kHandleOffset, &request.handle, sizeof(request.handle));
  std::memcpy(frame->data() + kOffsetOffset, &request.offset, sizeof(request.offset));
  std::memcpy(frame->data() + kSizeOffset, &request.size, sizeof(request.size));
  std::memcpy(frame->data() + kPathBytesOffset, &path_bytes, sizeof(path_bytes));
  if (path_size > 0) {
    std::memcpy(frame->data() + kHeaderSize, request.windows_name.data(), path_size);
  }

  return true;
}

bool KernelTransport::IsConnected() const { return connected_; }

const std::string& KernelTransport::LastError() const { return last_error_; }

}  // namespace jdrive64
