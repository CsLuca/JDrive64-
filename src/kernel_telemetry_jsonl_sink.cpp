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

KernelTelemetryJsonlSink::KernelTelemetryJsonlSink(std::string file_path, std::uintmax_t max_bytes)
    : file_path_(std::move(file_path)), max_bytes_(max_bytes == 0 ? kDefaultMaxBytes : max_bytes) {}

KernelTelemetryJsonlSink::KernelTelemetryJsonlSink(std::string file_path,
                                                   std::uintmax_t max_bytes,
                                                   std::size_t max_files)
    : file_path_(std::move(file_path)),
      max_bytes_(max_bytes == 0 ? kDefaultMaxBytes : max_bytes),
      max_files_(max_files == 0 ? kDefaultMaxFiles : max_files) {}

void KernelTelemetryJsonlSink::RotateIfNeeded() {
  std::error_code ec;
  const auto size = std::filesystem::file_size(file_path_, ec);
  if (ec) {
    return;
  }
  if (size < max_bytes_) {
    return;
  }

  for (std::size_t i = max_files_; i >= 1; --i) {
    const std::string src = i == 1 ? file_path_ : (file_path_ + "." + std::to_string(i - 1));
    const std::string dst = file_path_ + "." + std::to_string(i);
    std::filesystem::remove(dst, ec);
    ec.clear();
    if (!std::filesystem::exists(src)) {
      if (i == 1) {
        break;
      }
      continue;
    }
    std::filesystem::rename(src, dst, ec);
    if (ec) {
      last_error_ = "Cannot rotate telemetry JSONL file";
      return;
    }
    if (i == 1) {
      break;
    }
  }
}

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
  out.close();

  last_error_.clear();
  RotateIfNeeded();
}

const std::string& KernelTelemetryJsonlSink::LastError() const { return last_error_; }

}  // namespace jdrive64
