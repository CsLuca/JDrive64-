#pragma once

#include <mutex>
#include <string>

#include "jdrive64/kernel_transport.hpp"

namespace jdrive64 {

class KernelTelemetryJsonlSink final : public KernelTransport::TelemetrySink {
 public:
  static constexpr std::uintmax_t kDefaultMaxBytes = 256 * 1024;
  static constexpr std::size_t kDefaultMaxFiles = 3;

  explicit KernelTelemetryJsonlSink(std::string file_path);
  KernelTelemetryJsonlSink(std::string file_path, std::uintmax_t max_bytes);
  KernelTelemetryJsonlSink(std::string file_path, std::uintmax_t max_bytes, std::size_t max_files);

  void Emit(const KernelTransport::TelemetryEvent& event) override;

  const std::string& LastError() const;

 private:
  void RotateIfNeeded();

  std::string file_path_;
  std::uintmax_t max_bytes_ = kDefaultMaxBytes;
  std::size_t max_files_ = kDefaultMaxFiles;
  std::string last_error_;
  mutable std::mutex mutex_;
};

}  // namespace jdrive64
