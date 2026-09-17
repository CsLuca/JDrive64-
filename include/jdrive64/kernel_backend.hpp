#pragma once

#include <string>
#include <vector>

#include "jdrive64/kernel_ioctl_protocol.hpp"
#include "jdrive64/kernel_ipc_channel.hpp"

namespace jdrive64 {

class KernelBackendController {
 public:
  bool InstallService(const std::string& service_path);
  bool RemoveService();

  bool StartService(const std::string& image_path, const std::string& mount_point);
  bool StopService();

  bool ReadDirectory(std::vector<std::string>* entries);

  bool IsServiceRunning() const;
  const std::string& LastError() const;

 private:
  bool service_installed_ = false;
  bool service_running_ = false;
  KernelIpcChannel ipc_channel_;
  std::string last_error_;
};

}  // namespace jdrive64
