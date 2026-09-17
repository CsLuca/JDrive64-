#pragma once

#include <string>

namespace jdrive64 {

class KernelBackendController {
 public:
  bool InstallService(const std::string& service_path);
  bool RemoveService();

  bool StartService(const std::string& image_path, const std::string& mount_point);
  bool StopService();

  bool IsServiceRunning() const;
  const std::string& LastError() const;

 private:
  bool service_installed_ = false;
  bool service_running_ = false;
  std::string last_error_;
};

}  // namespace jdrive64
