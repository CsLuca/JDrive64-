#include "jdrive64/winfsp_callbacks.hpp"

namespace jdrive64 {

bool WinFspCallbacks::Initialize(WinFspFilesystem* filesystem) {
  last_error_.clear();

  if (initialized_) {
    last_error_ = "Callback table already initialized";
    return false;
  }
  if (filesystem == nullptr) {
    last_error_ = "Invalid filesystem instance";
    return false;
  }

  filesystem_ = filesystem;
  table_.get_volume_info = &WinFspCallbacks::TrampolineGetVolumeInfo;
  table_.read_directory = &WinFspCallbacks::TrampolineReadDirectory;
  table_.open = &WinFspCallbacks::TrampolineOpen;
  table_.read = &WinFspCallbacks::TrampolineRead;
  table_.close = &WinFspCallbacks::TrampolineClose;
  initialized_ = true;
  return true;
}

void WinFspCallbacks::Shutdown() {
  initialized_ = false;
  filesystem_ = nullptr;
  table_ = CallbackTable{};
}

bool WinFspCallbacks::IsInitialized() const { return initialized_; }

const std::string& WinFspCallbacks::LastError() const { return last_error_; }

const WinFspCallbacks::CallbackTable& WinFspCallbacks::Table() const { return table_; }

void* WinFspCallbacks::UserData() const { return filesystem_; }

bool WinFspCallbacks::DispatchGetVolumeInfo(WinFspFilesystem::VolumeInfo* info) const {
  if (!initialized_ || table_.get_volume_info == nullptr) {
    return false;
  }
  return table_.get_volume_info(filesystem_, info);
}

bool WinFspCallbacks::DispatchReadDirectory(std::vector<std::string>* entries) const {
  if (!initialized_ || table_.read_directory == nullptr) {
    return false;
  }
  return table_.read_directory(filesystem_, entries);
}

bool WinFspCallbacks::DispatchOpen(const std::string& windows_name, std::uint64_t* handle_out) const {
  if (!initialized_ || table_.open == nullptr) {
    return false;
  }
  return table_.open(filesystem_, windows_name, handle_out);
}

bool WinFspCallbacks::DispatchRead(std::uint64_t handle,
                                   std::uint64_t offset,
                                   std::uint32_t size,
                                   std::vector<std::uint8_t>* out_bytes) const {
  if (!initialized_ || table_.read == nullptr) {
    return false;
  }
  return table_.read(filesystem_, handle, offset, size, out_bytes);
}

bool WinFspCallbacks::DispatchClose(std::uint64_t handle) const {
  if (!initialized_ || table_.close == nullptr) {
    return false;
  }
  return table_.close(filesystem_, handle);
}

bool WinFspCallbacks::TrampolineGetVolumeInfo(void* user_data, WinFspFilesystem::VolumeInfo* info) {
  auto* fs = static_cast<WinFspFilesystem*>(user_data);
  return fs != nullptr && fs->GetVolumeInfo(info);
}

bool WinFspCallbacks::TrampolineReadDirectory(void* user_data, std::vector<std::string>* entries) {
  auto* fs = static_cast<WinFspFilesystem*>(user_data);
  if (fs == nullptr || entries == nullptr) {
    return false;
  }
  *entries = fs->ReadDirectory();
  return true;
}

bool WinFspCallbacks::TrampolineOpen(void* user_data,
                                     const std::string& windows_name,
                                     std::uint64_t* handle_out) {
  auto* fs = static_cast<WinFspFilesystem*>(user_data);
  return fs != nullptr && fs->Open(windows_name, handle_out);
}

bool WinFspCallbacks::TrampolineRead(void* user_data,
                                     std::uint64_t handle,
                                     std::uint64_t offset,
                                     std::uint32_t size,
                                     std::vector<std::uint8_t>* out_bytes) {
  auto* fs = static_cast<WinFspFilesystem*>(user_data);
  return fs != nullptr && fs->Read(handle, offset, size, out_bytes);
}

bool WinFspCallbacks::TrampolineClose(void* user_data, std::uint64_t handle) {
  auto* fs = static_cast<WinFspFilesystem*>(user_data);
  return fs != nullptr && fs->Close(handle);
}

}  // namespace jdrive64
