#pragma once

#include <mutex>
#include <string>

#include "jdrive64/kernel_transport.hpp"

namespace jdrive64 {

class KernelTelemetryJsonlSink final : public KernelTransport::TelemetrySink {
 public:
  explicit KernelTelemetryJsonlSink(std::string file_path);

  void Emit(const KernelTransport::TelemetryEvent& event) override;

  const std::string& LastError() const;

 private:
  std::string file_path_;
  std::string last_error_;
  mutable std::mutex mutex_;
};

}  // namespace jdrive64
