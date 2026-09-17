#pragma once

#include <memory>
#include <string>
#include <vector>

namespace jdrive64 {

class IMountBackend {
 public:
  virtual ~IMountBackend() = default;

  virtual bool MountReadOnly(const std::string& image_path, const std::string& mount_point) = 0;
  virtual bool Unmount(const std::string& mount_point) = 0;
  virtual bool HealthCheck() = 0;
  virtual std::vector<std::string> ReadDirectory() const = 0;
  virtual std::string GetVolumeInfoText() const = 0;
  virtual std::string GetBackendDiagnosticsText() const = 0;
  virtual const std::string& LastError() const = 0;
};

std::unique_ptr<IMountBackend> CreateMountBackend(const std::string& backend_name,
                                                  std::string* normalized_name_out,
                                                  std::string* error_out);

}  // namespace jdrive64
