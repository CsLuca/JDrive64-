#include "jdrive64/kernel_backend.hpp"

namespace jdrive64 {

bool KernelBackendController::InstallService(const std::string& service_path) {
  if (service_path.empty()) {
    last_error_ = "Invalid service path";
    return false;
  }

  service_installed_ = true;
  last_error_.clear();
  return true;
}

bool KernelBackendController::RemoveService() {
  if (!service_installed_) {
    last_error_ = "Kernel service is not installed";
    return false;
  }
  if (service_running_) {
    last_error_ = "Kernel service is still running";
    return false;
  }

  service_installed_ = false;
  last_error_.clear();
  return true;
}

bool KernelBackendController::StartService(const std::string& image_path,
                                           const std::string& mount_point) {
  if (!service_installed_) {
    last_error_ = "Kernel service is not installed";
    return false;
  }
  if (image_path.empty() || mount_point.empty()) {
    last_error_ = "Invalid start parameters";
    return false;
  }
  if (service_running_) {
    last_error_ = "Kernel service already running";
    return false;
  }

  if (!ipc_channel_.Connect(image_path)) {
    last_error_ = ipc_channel_.LastError();
    return false;
  }

  service_running_ = true;
  last_error_.clear();
  return true;
}

bool KernelBackendController::StopService() {
  if (!service_running_) {
    last_error_ = "Kernel service is not running";
    return false;
  }

  if (!ipc_channel_.Disconnect()) {
    last_error_ = ipc_channel_.LastError();
    return false;
  }

  service_running_ = false;
  last_error_.clear();
  return true;
}

bool KernelBackendController::ReadDirectory(std::vector<std::string>* entries) {
  if (entries == nullptr) {
    last_error_ = "Invalid output entries";
    return false;
  }
  if (!service_running_) {
    last_error_ = "Kernel service is not running";
    return false;
  }

  KernelResponse response;
  const KernelRequest request{KernelOpcode::kReadDirectory, "", 0, 0, 0};
  if (!ipc_channel_.Send(request, &response)) {
    last_error_ = ipc_channel_.LastError();
    return false;
  }

  *entries = response.directory_entries;
  last_error_.clear();
  return true;
}

bool KernelBackendController::IsServiceRunning() const { return service_running_; }

const std::string& KernelBackendController::LastError() const { return last_error_; }

}  // namespace jdrive64
