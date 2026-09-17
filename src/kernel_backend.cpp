#include "jdrive64/kernel_backend.hpp"

#include <sstream>

#include "jdrive64/kernel_telemetry_jsonl_sink.hpp"

namespace jdrive64 {

bool KernelBackendController::SetTransportMode(KernelTransport::Mode mode) {
  if (!ipc_channel_.SetTransportMode(mode)) {
    last_error_ = ipc_channel_.LastError();
    return false;
  }
  last_error_.clear();
  return true;
}

KernelTransport::Mode KernelBackendController::GetTransportMode() const {
  return ipc_channel_.GetTransportMode();
}

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

bool KernelBackendController::EnableTelemetryJsonl(const std::string& file_path) {
  telemetry_sink_ = std::make_unique<KernelTelemetryJsonlSink>(file_path);
  ipc_channel_.SetTelemetrySinkForTesting(telemetry_sink_.get());
  return true;
}

bool KernelBackendController::EnableTelemetryJsonl(const std::string& file_path,
                                                   std::uintmax_t max_bytes,
                                                   std::size_t max_files) {
  telemetry_sink_ = std::make_unique<KernelTelemetryJsonlSink>(file_path, max_bytes, max_files);
  ipc_channel_.SetTelemetrySinkForTesting(telemetry_sink_.get());
  return true;
}

std::string KernelBackendController::GetDiagnosticsText() const {
  auto mode_to_string = [](KernelTransport::Mode mode) {
    return mode == KernelTransport::Mode::kDevice ? "device" : "loopback";
  };
  auto policy_to_string = [](KernelTransport::FeaturePolicy policy) {
    return policy == KernelTransport::FeaturePolicy::kBestEffort ? "best_effort" : "strict";
  };

  std::ostringstream out;
  out << "Backend=kdrv\n";
  out << "ServiceRunning=" << (service_running_ ? "yes" : "no") << "\n";
  out << "TransportMode=" << mode_to_string(ipc_channel_.GetTransportMode()) << "\n";
  out << "FeaturePolicy=" << policy_to_string(ipc_channel_.GetFeaturePolicy()) << "\n";
  out << "HandshakeComplete=" << (ipc_channel_.IsHandshakeComplete() ? "yes" : "no") << "\n";
  out << "NegotiatedProtocol=" << ipc_channel_.NegotiatedProtocolVersion() << "\n";
  out << "NegotiatedCapabilities=0x" << std::hex << ipc_channel_.NegotiatedCapabilities() << std::dec
      << "\n";
  out << "NegotiatedFeatures=0x" << std::hex << ipc_channel_.NegotiatedFeatures() << std::dec;
  out << "\nTelemetrySink=" << (ipc_channel_.HasTelemetrySink() ? "enabled" : "disabled");
  if (telemetry_sink_) {
    const auto& err = telemetry_sink_->LastError();
    if (!err.empty()) {
      out << "\nTelemetryLastError=" << err;
    }
  }
  return out.str();
}

bool KernelBackendController::IsServiceRunning() const { return service_running_; }

const std::string& KernelBackendController::LastError() const { return last_error_; }

}  // namespace jdrive64
