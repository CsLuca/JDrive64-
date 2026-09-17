#include "jdrive64/kernel_telemetry_jsonl_sink.hpp"

#include <filesystem>
#include <fstream>

namespace jdrive64 {

namespace {

std::string EscapeJson(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

}  // namespace

KernelTelemetryJsonlSink::KernelTelemetryJsonlSink(std::string file_path)
    : file_path_(std::move(file_path)) {}

void KernelTelemetryJsonlSink::Emit(const KernelTransport::TelemetryEvent& event) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (file_path_.empty()) {
    last_error_ = "Telemetry JSONL path is empty";
    return;
  }

  std::error_code ec;
  const auto parent = std::filesystem::path(file_path_).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      last_error_ = "Cannot create telemetry directory";
      return;
    }
  }

  std::ofstream out(file_path_, std::ios::binary | std::ios::app);
  if (!out) {
    last_error_ = "Cannot open telemetry JSONL file";
    return;
  }

  out << "{\"name\":\"" << EscapeJson(event.name) << "\","
      << "\"success\":" << (event.success ? "true" : "false") << ","
      << "\"detail\":\"" << EscapeJson(event.detail) << "\"}\n";
  if (!out) {
    last_error_ = "Cannot write telemetry JSONL event";
    return;
  }

  last_error_.clear();
}

const std::string& KernelTelemetryJsonlSink::LastError() const { return last_error_; }

}  // namespace jdrive64
