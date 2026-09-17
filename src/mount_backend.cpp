#include "jdrive64/mount_backend.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

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

  const std::string& LastError() const override { return filesystem_.LastError(); }

 private:
  WinFspFilesystem filesystem_;
};

class KernelMountBackend final : public IMountBackend {
 public:
  bool MountReadOnly(const std::string& image_path, const std::string& mount_point) override {
    (void)image_path;
    (void)mount_point;
    last_error_ = "Kernel backend not implemented";
    return false;
  }

  bool Unmount(const std::string& mount_point) override {
    (void)mount_point;
    last_error_ = "Kernel backend not implemented";
    return false;
  }

  bool HealthCheck() override {
    last_error_ = "Kernel backend not implemented";
    return false;
  }

  std::vector<std::string> ReadDirectory() const override { return {}; }

  std::string GetVolumeInfoText() const override { return ""; }

  const std::string& LastError() const override { return last_error_; }

 private:
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
