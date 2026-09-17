#include "jdrive64/kernel_user_bridge.hpp"

namespace jdrive64 {

bool KernelUserBridge::Initialize(const std::string& image_path) {
  if (!filesystem_.OpenImage(image_path)) {
    last_error_ = filesystem_.LastError();
    return false;
  }

  initialized_ = true;
  last_error_.clear();
  return true;
}

bool KernelUserBridge::Dispatch(const KernelRequest& request, KernelResponse* response) {
  if (response == nullptr) {
    last_error_ = "Invalid response output";
    return false;
  }

  *response = KernelResponse{};
  if (!initialized_) {
    response->error = "Bridge is not initialized";
    return false;
  }

  switch (request.opcode) {
    case KernelOpcode::kReadDirectory: {
      response->success = filesystem_.ReadDirectory(&response->directory_entries);
      if (!response->success) {
        response->error = "ReadDirectory failed";
      }
      return response->success;
    }
    case KernelOpcode::kQueryFile: {
      KernelReadOnlyFilesystem::FileInfo info;
      response->success = filesystem_.QueryFile(request.windows_name, &info);
      if (!response->success) {
        response->error = "QueryFile failed";
        return false;
      }
      response->file_name = info.windows_name;
      response->file_size_bytes = info.size_bytes;
      return true;
    }
    case KernelOpcode::kOpenFile: {
      response->success = filesystem_.OpenFile(request.windows_name, &response->handle);
      if (!response->success) {
        response->error = filesystem_.LastError();
      }
      return response->success;
    }
    case KernelOpcode::kReadFile: {
      response->success = filesystem_.ReadFile(request.handle, request.offset, request.size, &response->data);
      if (!response->success) {
        response->error = filesystem_.LastError();
      }
      return response->success;
    }
    case KernelOpcode::kCloseFile: {
      response->success = filesystem_.CloseFile(request.handle);
      if (!response->success) {
        response->error = filesystem_.LastError();
      }
      return response->success;
    }
    case KernelOpcode::kInvalid:
    default:
      response->error = "Unsupported opcode";
      return false;
  }
}

const std::string& KernelUserBridge::LastError() const { return last_error_; }

}  // namespace jdrive64
