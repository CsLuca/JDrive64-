#include "jdrive64/kernel_readonly_fs.hpp"

#include <algorithm>

namespace jdrive64 {

bool KernelReadOnlyFilesystem::OpenImage(const std::string& image_path) {
  if (!session_.Open(image_path)) {
    last_error_ = session_.LastError();
    return false;
  }

  open_handles_.clear();
  next_handle_ = 1;
  last_error_.clear();
  return true;
}

bool KernelReadOnlyFilesystem::ReadDirectory(std::vector<std::string>* entries) const {
  if (entries == nullptr) {
    return false;
  }

  entries->clear();
  for (const auto& file : session_.Catalog().Files()) {
    entries->push_back(file.windows_name);
  }
  std::sort(entries->begin(), entries->end());
  return true;
}

bool KernelReadOnlyFilesystem::QueryFile(const std::string& windows_name, FileInfo* info) const {
  if (info == nullptr) {
    return false;
  }

  const auto* file = session_.Catalog().FindByWindowsName(windows_name);
  if (file == nullptr) {
    return false;
  }

  std::vector<std::uint8_t> bytes;
  if (!const_cast<DiskImageSession&>(session_).ReadFileByCatalogFile(*file, &bytes)) {
    return false;
  }

  info->windows_name = file->windows_name;
  info->size_bytes = bytes.size();
  return true;
}

bool KernelReadOnlyFilesystem::OpenFile(const std::string& windows_name, std::uint64_t* handle_out) {
  if (handle_out == nullptr) {
    last_error_ = "Invalid output handle";
    return false;
  }

  const auto* file = session_.Catalog().FindByWindowsName(windows_name);
  if (file == nullptr) {
    last_error_ = "File not found";
    return false;
  }

  const std::uint64_t handle = next_handle_++;
  open_handles_[handle] = file->windows_name;
  *handle_out = handle;
  last_error_.clear();
  return true;
}

bool KernelReadOnlyFilesystem::ReadFile(std::uint64_t handle,
                                        std::uint64_t offset,
                                        std::uint32_t size,
                                        std::vector<std::uint8_t>* out_bytes) {
  if (out_bytes == nullptr) {
    last_error_ = "Invalid output buffer";
    return false;
  }
  if (!IsValidHandle(handle)) {
    last_error_ = "Invalid handle";
    return false;
  }

  const auto it = open_handles_.find(handle);
  std::vector<std::uint8_t> bytes;
  if (!session_.ReadFileByWindowsName(it->second, &bytes)) {
    last_error_ = session_.LastError();
    return false;
  }

  out_bytes->clear();
  if (offset >= bytes.size() || size == 0) {
    return true;
  }

  const auto start = static_cast<std::size_t>(offset);
  const auto remaining = bytes.size() - start;
  const auto count = std::min<std::size_t>(remaining, size);
  out_bytes->insert(out_bytes->end(), bytes.begin() + static_cast<std::ptrdiff_t>(start),
                    bytes.begin() + static_cast<std::ptrdiff_t>(start + count));
  return true;
}

bool KernelReadOnlyFilesystem::CloseFile(std::uint64_t handle) {
  if (!IsValidHandle(handle)) {
    last_error_ = "Invalid handle";
    return false;
  }
  open_handles_.erase(handle);
  last_error_.clear();
  return true;
}

const std::string& KernelReadOnlyFilesystem::LastError() const { return last_error_; }

bool KernelReadOnlyFilesystem::IsValidHandle(std::uint64_t handle) const {
  return open_handles_.find(handle) != open_handles_.end();
}

}  // namespace jdrive64
