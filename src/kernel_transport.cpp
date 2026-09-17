#include "jdrive64/kernel_transport.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <cstring>

namespace jdrive64 {

namespace {

constexpr std::size_t kOpcodeOffset = 0;
constexpr std::size_t kHandleOffset = kOpcodeOffset + sizeof(std::uint32_t);
constexpr std::size_t kOffsetOffset = kHandleOffset + sizeof(std::uint64_t);
constexpr std::size_t kSizeOffset = kOffsetOffset + sizeof(std::uint64_t);
constexpr std::size_t kPathBytesOffset = kSizeOffset + sizeof(std::uint32_t);

constexpr std::size_t kSuccessOffset = 0;
constexpr std::size_t kPayloadBytesOffset = kSuccessOffset + sizeof(std::uint32_t);
constexpr std::size_t kHandleOutOffset = kPayloadBytesOffset + sizeof(std::uint32_t);
constexpr std::size_t kErrorBytesOffset = kHandleOutOffset + sizeof(std::uint64_t);

}  // namespace

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

    KernelResponse parsed;
    if (!ParseDeviceFrame({}, &parsed)) {
      response->success = false;
      response->error = "Device response parsing failed";
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

  const std::size_t path_size = request.windows_name.size();
  frame->assign(kDeviceRequestHeaderSize + path_size, 0);

  const std::uint32_t opcode = static_cast<std::uint32_t>(request.opcode);
  const std::uint32_t path_bytes = static_cast<std::uint32_t>(path_size);
  std::memcpy(frame->data() + kOpcodeOffset, &opcode, sizeof(opcode));
  std::memcpy(frame->data() + kHandleOffset, &request.handle, sizeof(request.handle));
  std::memcpy(frame->data() + kOffsetOffset, &request.offset, sizeof(request.offset));
  std::memcpy(frame->data() + kSizeOffset, &request.size, sizeof(request.size));
  std::memcpy(frame->data() + kPathBytesOffset, &path_bytes, sizeof(path_bytes));
  if (path_size > 0) {
    std::memcpy(frame->data() + kDeviceRequestHeaderSize, request.windows_name.data(), path_size);
  }

  return true;
}

bool KernelTransport::ParseDeviceFrame(const std::vector<std::uint8_t>& frame,
                                       KernelResponse* response) const {
  if (response == nullptr) {
    return false;
  }

  *response = KernelResponse{};
  if (frame.empty()) {
    response->success = false;
    response->error = "Device response frame is empty";
    return true;
  }
  if (frame.size() < kDeviceResponseHeaderSize) {
    response->success = false;
    response->error = "Device response frame is truncated";
    return true;
  }

  std::uint32_t success = 0;
  std::uint32_t payload_bytes = 0;
  std::uint64_t handle = 0;
  std::uint32_t error_bytes = 0;
  std::memcpy(&success, frame.data() + kSuccessOffset, sizeof(success));
  std::memcpy(&payload_bytes, frame.data() + kPayloadBytesOffset, sizeof(payload_bytes));
  std::memcpy(&handle, frame.data() + kHandleOutOffset, sizeof(handle));
  std::memcpy(&error_bytes, frame.data() + kErrorBytesOffset, sizeof(error_bytes));

  const std::size_t expected_min = kDeviceResponseHeaderSize + static_cast<std::size_t>(payload_bytes) +
                                   static_cast<std::size_t>(error_bytes);
  if (frame.size() < expected_min) {
    response->success = false;
    response->error = "Device response payload is truncated";
    return true;
  }

  response->success = (success != 0);
  response->handle = handle;
  if (payload_bytes > 0) {
    const std::uint8_t* payload = frame.data() + kDeviceResponseHeaderSize;
    response->data.assign(payload, payload + payload_bytes);
  }
  if (error_bytes > 0) {
    const char* error_ptr = reinterpret_cast<const char*>(
        frame.data() + kDeviceResponseHeaderSize + static_cast<std::size_t>(payload_bytes));
    response->error.assign(error_ptr, error_bytes);
  }

  return true;
}

bool KernelTransport::IsConnected() const { return connected_; }

const std::string& KernelTransport::LastError() const { return last_error_; }

}  // namespace jdrive64
