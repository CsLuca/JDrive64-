#include "jdrive64/mount_backend.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

#include "jdrive64/kernel_backend.hpp"
#include "jdrive64/kernel_mount_manager.hpp"
#include "jdrive64/winfsp_filesystem.hpp"

namespace jdrive64 {

namespace {

class WinFspMountBackend final : public IMountBackend {
 public:
  bool MountReadOnly(const std::string& image_path, const std::string& mount_point) override {
    return filesystem_.MountReadOnly(image_path, mount_point);
  }

  bool Unmount(const std::string& mount_point) override { return filesystem_.Unmount(mount_point); }

  bool HealthCheck() override { return filesystem_.IsMounted(); }

  std::vector<std::string> ReadDirectory() const override { return filesystem_.ReadDirectory(); }

  std::string GetVolumeInfoText() const override { return filesystem_.GetVolumeInfoText(); }

  std::string GetBackendDiagnosticsText() const override {
    return "Backend=winfsp\nMode=native\nNegotiatedProtocol=n/a\nNegotiatedCapabilities=n/a\n"
           "NegotiatedFeatures=n/a\nFeaturePolicy=n/a\nHandshakeComplete=n/a";
  }

  const std::string& LastError() const override { return filesystem_.LastError(); }

 private:
  WinFspFilesystem filesystem_;
};

class KernelMountBackend final : public IMountBackend {
 public:
  bool MountReadOnly(const std::string& image_path, const std::string& mount_point) override {
    if (!controller_.SetTransportMode(KernelTransport::Mode::kLoopback)) {
      last_error_ = controller_.LastError();
      return false;
    }

    const char* telemetry_path = std::getenv("JDRIVE64_TELEMETRY_JSONL");
    if (telemetry_path != nullptr && *telemetry_path != '\0') {
      if (!controller_.EnableTelemetryJsonl(telemetry_path)) {
        last_error_ = controller_.LastError();
        return false;
      }
    }

    if (!controller_.InstallService("jdrive64ksvc.exe")) {
      last_error_ = controller_.LastError();
      return false;
    }
    if (!controller_.StartService(image_path, mount_point)) {
      last_error_ = controller_.LastError();
      return false;
    }

    if (!mount_manager_.AssignDriveLetter(mount_point)) {
      last_error_ = mount_manager_.LastError();
      controller_.StopService();
      controller_.RemoveService();
      return false;
    }

    mounted_ = true;
    last_error_.clear();
    return true;
  }

  bool Unmount(const std::string& mount_point) override {
    (void)mount_point;

    if (!mounted_) {
      last_error_ = "Kernel backend not mounted";
      return false;
    }
    if (!mount_manager_.ReleaseDriveLetter(mount_point)) {
      last_error_ = mount_manager_.LastError();
      return false;
    }
    if (!controller_.StopService()) {
      last_error_ = controller_.LastError();
      return false;
    }
    if (!controller_.RemoveService()) {
      last_error_ = controller_.LastError();
      return false;
    }

    mounted_ = false;
    last_error_.clear();
    return true;
  }

  bool HealthCheck() override { return mounted_ && controller_.IsServiceRunning(); }

  std::vector<std::string> ReadDirectory() const override {
    std::vector<std::string> entries;
    if (!const_cast<KernelBackendController&>(controller_).ReadDirectory(&entries)) {
      return {};
    }
    return entries;
  }

  std::string GetVolumeInfoText() const override {
    return "Kernel backend scaffold active (K7 transport loopback mode)";
  }

  std::string GetBackendDiagnosticsText() const override {
    return controller_.GetDiagnosticsText();
  }

  const std::string& LastError() const override { return last_error_; }

 private:
  bool mounted_ = false;
  KernelBackendController controller_;
  KernelMountManager mount_manager_;
  std::string last_error_;
};

std::string NormalizeBackendName(std::string backend) {
  std::transform(backend.begin(), backend.end(), backend.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return backend;
}

}  // namespace

std::unique_ptr<IMountBackend> CreateMountBackend(const std::string& backend_name,
                                                  std::string* normalized_name_out,
                                                  std::string* error_out) {
  if (normalized_name_out == nullptr || error_out == nullptr) {
    return nullptr;
  }

  const std::string normalized = NormalizeBackendName(backend_name.empty() ? "winfsp" : backend_name);
  *normalized_name_out = normalized;

  if (normalized == "winfsp") {
    *error_out = "";
    return std::make_unique<WinFspMountBackend>();
  }

  if (normalized == "kdrv") {
    *error_out = "";
    return std::make_unique<KernelMountBackend>();
  }

  *error_out = "Unknown backend: " + backend_name + " (supported: winfsp, kdrv)";
  return nullptr;
}

}  // namespace jdrive64
