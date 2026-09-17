#pragma once

#include <string>
#include <memory>
#include <vector>

#include "jdrive64/kernel_ioctl_protocol.hpp"
#include "jdrive64/kernel_ipc_channel.hpp"
#include "jdrive64/kernel_telemetry_jsonl_sink.hpp"

namespace jdrive64 {

class KernelBackendController {
 public:
  bool SetTransportMode(KernelTransport::Mode mode);
  KernelTransport::Mode GetTransportMode() const;

  bool InstallService(const std::string& service_path);
  bool RemoveService();

  bool StartService(const std::string& image_path, const std::string& mount_point);
  bool StopService();

  bool ReadDirectory(std::vector<std::string>* entries);

  bool EnableTelemetryJsonl(const std::string& file_path);
  bool EnableTelemetryJsonl(const std::string& file_path, std::uintmax_t max_bytes, std::size_t max_files);

  std::string GetDiagnosticsText() const;

  bool IsServiceRunning() const;
  const std::string& LastError() const;

 private:
  std::unique_ptr<KernelTelemetryJsonlSink> telemetry_sink_;
  bool service_installed_ = false;
  bool service_running_ = false;
  KernelIpcChannel ipc_channel_;
  std::string last_error_;
};

}  // namespace jdrive64
