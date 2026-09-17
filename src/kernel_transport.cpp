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

constexpr const char* kDevicePath = "\\\\.\\JDrive64Kdrv";

#if defined(_WIN32)
constexpr DWORD kIoctlJDrive64Request = 0x00222000;

class WinDeviceIoApi final : public KernelTransport::DeviceIoApi {
 public:
  bool Open(const std::string& device_path, void** handle, std::string* error) override {
    if (handle == nullptr || error == nullptr) {
      return false;
    }

    HANDLE h = CreateFileA(device_path.c_str(),
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      *error = "Kernel device channel not available";
      return false;
    }

    *handle = h;
    error->clear();
    return true;
  }

  bool Close(void* handle, std::string* error) override {
    if (error == nullptr) {
      return false;
    }
    if (handle == nullptr) {
      *error = "Invalid device handle";
      return false;
    }
    if (!CloseHandle(static_cast<HANDLE>(handle))) {
      *error = "Failed to close device handle";
      return false;
    }
    error->clear();
    return true;
  }

  bool Ioctl(void* handle,
             const std::vector<std::uint8_t>& request_frame,
             std::vector<std::uint8_t>* response_frame,
             std::string* error) override {
    if (handle == nullptr || response_frame == nullptr || error == nullptr) {
      return false;
    }

    std::vector<std::uint8_t> out(64 * 1024, 0);
    DWORD bytes_returned = 0;
    const BOOL ok = DeviceIoControl(static_cast<HANDLE>(handle),
                                    kIoctlJDrive64Request,
                                    const_cast<std::uint8_t*>(request_frame.data()),
                                    static_cast<DWORD>(request_frame.size()),
                                    out.data(),
                                    static_cast<DWORD>(out.size()),
                                    &bytes_returned,
                                    nullptr);
    if (!ok) {
      *error = "DeviceIoControl failed";
      return false;
    }

    out.resize(bytes_returned);
    *response_frame = std::move(out);
    error->clear();
    return true;
  }
};
#endif

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

bool KernelTransport::SetDeviceIoApiForTesting(DeviceIoApi* api) {
  if (connected_) {
    last_error_ = "Cannot change device IO API while connected";
    return false;
  }
  device_io_api_ = api;
  last_error_.clear();
  return true;
}

KernelTransport::DeviceIoApi* KernelTransport::ResolveDeviceIoApi() {
  if (device_io_api_ != nullptr) {
    return device_io_api_;
  }

#if defined(_WIN32)
  if (!default_device_io_api_) {
    default_device_io_api_ = std::make_unique<WinDeviceIoApi>();
  }
  device_io_api_ = default_device_io_api_.get();
  return device_io_api_;
#else
  return nullptr;
#endif
}

bool KernelTransport::Connect(const std::string& image_path) {
  if (connected_) {
    last_error_ = "Transport already connected";
    return false;
  }

  handshake_complete_ = false;
  negotiated_protocol_version_ = 0;
  negotiated_capabilities_ = 0;
  negotiated_features_ = 0;

  if (mode_ == Mode::kLoopback) {
    if (!bridge_.Initialize(image_path)) {
      last_error_ = bridge_.LastError();
      return false;
    }
    connected_ = true;
    handshake_complete_ = true;
    negotiated_protocol_version_ = kKernelProtocolVersionCurrent;
    negotiated_capabilities_ = kKernelCapabilityAllReadOnly;
    negotiated_features_ = kKernelFeatureDefault;
    last_error_.clear();
    return true;
  }

  (void)image_path;
  DeviceIoApi* api = ResolveDeviceIoApi();
  if (api == nullptr) {
    last_error_ = "Device transport is only supported on Windows";
    return false;
  }

  std::string error;
  void* handle = nullptr;
  if (!api->Open(kDevicePath, &handle, &error)) {
    last_error_ = error.empty() ? "Kernel device channel not available" : error;
    return false;
  }

  device_handle_ = handle;
  connected_ = true;

  KernelResponse handshake_response;
  const KernelRequest handshake_request{
      KernelOpcode::kHandshake,
      "",
      static_cast<std::uint64_t>(kKernelProtocolVersionCurrent),
      static_cast<std::uint64_t>(kKernelCapabilityAllReadOnly),
      kKernelFeatureDefault,
  };
  if (!Send(handshake_request, &handshake_response)) {
    std::string close_error;
    api->Close(device_handle_, &close_error);
    device_handle_ = nullptr;
    connected_ = false;
    handshake_complete_ = false;
    negotiated_protocol_version_ = 0;
    negotiated_capabilities_ = 0;
    negotiated_features_ = 0;
    if (last_error_.empty()) {
      last_error_ = "Kernel handshake failed";
    }
    return false;
  }

  if (handshake_response.data.size() < sizeof(std::uint32_t) + sizeof(std::uint32_t)) {
    std::string close_error;
    api->Close(device_handle_, &close_error);
    device_handle_ = nullptr;
    connected_ = false;
    handshake_complete_ = false;
    negotiated_protocol_version_ = 0;
    negotiated_capabilities_ = 0;
    negotiated_features_ = 0;
    last_error_ = "Kernel handshake response is invalid";
    return false;
  }

  std::uint32_t protocol_version = 0;
  std::uint32_t capabilities = 0;
  std::uint32_t features = 0;
  std::memcpy(&protocol_version, handshake_response.data.data(), sizeof(protocol_version));
  std::memcpy(&capabilities,
              handshake_response.data.data() + sizeof(protocol_version),
              sizeof(capabilities));
  if (handshake_response.data.size() >= sizeof(protocol_version) + sizeof(capabilities) +
                                        sizeof(features)) {
    std::memcpy(&features,
                handshake_response.data.data() + sizeof(protocol_version) + sizeof(capabilities),
                sizeof(features));
  } else {
    features = kKernelFeatureStrictReadonly;
  }

  if (protocol_version < kKernelProtocolVersionMin || protocol_version > kKernelProtocolVersionMax) {
    std::string close_error;
    api->Close(device_handle_, &close_error);
    device_handle_ = nullptr;
    connected_ = false;
    handshake_complete_ = false;
    negotiated_protocol_version_ = 0;
    negotiated_capabilities_ = 0;
    negotiated_features_ = 0;
    last_error_ = "Kernel protocol version mismatch";
    return false;
  }

  if ((capabilities & kKernelCapabilityAllReadOnly) != kKernelCapabilityAllReadOnly) {
    std::string close_error;
    api->Close(device_handle_, &close_error);
    device_handle_ = nullptr;
    connected_ = false;
    handshake_complete_ = false;
    negotiated_protocol_version_ = 0;
    negotiated_capabilities_ = 0;
    negotiated_features_ = 0;
    last_error_ = "Kernel capabilities are insufficient";
    return false;
  }

  if ((features & kKernelFeatureStrictReadonly) == 0) {
    std::string close_error;
    api->Close(device_handle_, &close_error);
    device_handle_ = nullptr;
    connected_ = false;
    handshake_complete_ = false;
    negotiated_protocol_version_ = 0;
    negotiated_capabilities_ = 0;
    negotiated_features_ = 0;
    last_error_ = "Kernel features are incompatible";
    return false;
  }

  handshake_complete_ = true;
  negotiated_protocol_version_ = protocol_version;
  negotiated_capabilities_ = capabilities;
  negotiated_features_ = features;
  last_error_.clear();
  return true;
}

bool KernelTransport::Disconnect() {
  if (!connected_) {
    last_error_ = "Transport is not connected";
    return false;
  }

  if (mode_ == Mode::kLoopback) {
    connected_ = false;
    handshake_complete_ = false;
    negotiated_protocol_version_ = 0;
    negotiated_capabilities_ = 0;
    negotiated_features_ = 0;
    last_error_.clear();
    return true;
  }

  DeviceIoApi* api = ResolveDeviceIoApi();
  if (api == nullptr) {
    last_error_ = "Device transport is only supported on Windows";
    return false;
  }

  std::string error;
  if (!api->Close(device_handle_, &error)) {
    last_error_ = error.empty() ? "Failed to close device handle" : error;
    return false;
  }

  device_handle_ = nullptr;
  connected_ = false;
  handshake_complete_ = false;
  negotiated_protocol_version_ = 0;
  negotiated_capabilities_ = 0;
  negotiated_features_ = 0;
  last_error_.clear();
  return true;
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

  if (response == nullptr) {
    last_error_ = "Invalid response output";
    return false;
  }

  if (request.opcode != KernelOpcode::kHandshake && !handshake_complete_) {
    response->success = false;
    response->error = "Kernel handshake is not completed";
    last_error_ = response->error;
    return false;
  }

  std::vector<std::uint8_t> request_frame;
  if (!BuildDeviceFrame(request, &request_frame)) {
    response->success = false;
    response->error = "Device request encoding failed";
    last_error_ = response->error;
    return false;
  }

  DeviceIoApi* api = ResolveDeviceIoApi();
  if (api == nullptr) {
    response->success = false;
    response->error = "Device transport is only supported on Windows";
    last_error_ = response->error;
    return false;
  }

  std::vector<std::uint8_t> response_frame;
  std::string error;
  if (!api->Ioctl(device_handle_, request_frame, &response_frame, &error)) {
    response->success = false;
    response->error = error.empty() ? "DeviceIoControl failed" : error;
    last_error_ = response->error;
    return false;
  }

  if (!ParseDeviceFrame(response_frame, response)) {
    response->success = false;
    response->error = "Device response parsing failed";
    last_error_ = response->error;
    return false;
  }

  if (!response->success) {
    if (response->error.empty()) {
      response->error = "Device request failed";
    }
    last_error_ = response->error;
    return false;
  }

  last_error_.clear();
  return true;
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

bool KernelTransport::IsHandshakeComplete() const { return handshake_complete_; }

std::uint32_t KernelTransport::NegotiatedProtocolVersion() const {
  return negotiated_protocol_version_;
}

std::uint32_t KernelTransport::NegotiatedCapabilities() const { return negotiated_capabilities_; }

std::uint32_t KernelTransport::NegotiatedFeatures() const { return negotiated_features_; }

const std::string& KernelTransport::LastError() const { return last_error_; }

}  // namespace jdrive64
