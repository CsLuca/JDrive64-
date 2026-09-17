#include "jdrive64/winfsp_native_api.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace jdrive64 {

namespace {

#if defined(_WIN32)
using FspFileSystemCreateFn = unsigned long (*)(
    const wchar_t* device_name,
    void* volume_params,
    void* interface_params,
    void** file_system_out);
using FspFileSystemSetMountPointFn = unsigned long (*)(void* file_system, const wchar_t* mount_point);
using FspFileSystemStartDispatcherFn = unsigned long (*)(void* file_system, unsigned long thread_count);
using FspFileSystemStopDispatcherFn = void (*)(void* file_system);
using FspFileSystemDeleteFn = void (*)(void* file_system);
#endif

}  // namespace

DefaultWinFspNativeApi::~DefaultWinFspNativeApi() {
#if defined(_WIN32)
  if (dispatcher_started_ && proc_stop_dispatcher_ != nullptr && file_system_handle_ != nullptr) {
    auto stop_dispatcher = reinterpret_cast<FspFileSystemStopDispatcherFn>(proc_stop_dispatcher_);
    stop_dispatcher(file_system_handle_);
    dispatcher_started_ = false;
  }
  if (file_system_handle_ != nullptr && proc_delete_ != nullptr) {
    auto delete_fs = reinterpret_cast<FspFileSystemDeleteFn>(proc_delete_);
    delete_fs(file_system_handle_);
    file_system_handle_ = nullptr;
  }
  if (module_handle_ != nullptr) {
    FreeLibrary(static_cast<HMODULE>(module_handle_));
    module_handle_ = nullptr;
  }
#endif
}

bool DefaultWinFspNativeApi::EnsureBootstrapReady(std::string* error_out) {
  if (error_out == nullptr) {
    return false;
  }

#if defined(JDRIVE64_ENABLE_WINFSP) && defined(_WIN32)
  if (module_handle_ == nullptr) {
    const char* candidates[] = {"winfsp-x64.dll", "winfsp.dll"};
    for (const char* candidate : candidates) {
      HMODULE module = LoadLibraryA(candidate);
      if (module != nullptr) {
        module_handle_ = module;
        break;
      }
    }
    if (module_handle_ == nullptr) {
      *error_out = "Cannot load WinFsp DLL (winfsp-x64.dll/winfsp.dll)";
      return false;
    }
  }

  const char* required_symbols[] = {
      "FspFileSystemCreate",
      "FspFileSystemSetMountPoint",
      "FspFileSystemStartDispatcher",
      "FspFileSystemStopDispatcher",
      "FspFileSystemDelete",
  };

  for (const char* symbol : required_symbols) {
    FARPROC proc = GetProcAddress(static_cast<HMODULE>(module_handle_), symbol);
    if (proc == nullptr) {
      *error_out = std::string("WinFsp symbol not found: ") + symbol;
      return false;
    }
    if (std::string(symbol) == "FspFileSystemCreate") {
      proc_create_ = reinterpret_cast<void*>(proc);
    } else if (std::string(symbol) == "FspFileSystemSetMountPoint") {
      proc_set_mount_point_ = reinterpret_cast<void*>(proc);
    } else if (std::string(symbol) == "FspFileSystemStartDispatcher") {
      proc_start_dispatcher_ = reinterpret_cast<void*>(proc);
    } else if (std::string(symbol) == "FspFileSystemStopDispatcher") {
      proc_stop_dispatcher_ = reinterpret_cast<void*>(proc);
    } else if (std::string(symbol) == "FspFileSystemDelete") {
      proc_delete_ = reinterpret_cast<void*>(proc);
    }
  }

  *error_out = "";
  return true;
#else
  (void)error_out;
  return false;
#endif
}

bool DefaultWinFspNativeApi::RegisterReadOnly(const std::string& mount_point,
                                              const WinFspCallbacks& callbacks,
                                              std::string* error_out) {
  if (error_out == nullptr) {
    return false;
  }

#if defined(JDRIVE64_ENABLE_WINFSP)
  if (mount_point.empty()) {
    *error_out = "Invalid mount point";
    return false;
  }
  if (!callbacks.IsInitialized()) {
    *error_out = "Callback table is not initialized";
    return false;
  }

  if (registration_active_) {
    *error_out = "WinFsp native API already registered";
    return false;
  }

  if (!EnsureBootstrapReady(error_out)) {
    if (error_out->empty()) {
      *error_out = "WinFsp bootstrap failed";
    }
    return false;
  }

#if defined(_WIN32)
  auto create_fs = reinterpret_cast<FspFileSystemCreateFn>(proc_create_);
  auto set_mount = reinterpret_cast<FspFileSystemSetMountPointFn>(proc_set_mount_point_);
  auto start_dispatcher = reinterpret_cast<FspFileSystemStartDispatcherFn>(proc_start_dispatcher_);
  auto delete_fs = reinterpret_cast<FspFileSystemDeleteFn>(proc_delete_);

  void* fs_handle = nullptr;
  const unsigned long nt_status_create =
      create_fs(L"JDRIVE64", nullptr, callbacks.UserData(), &fs_handle);
  if (nt_status_create != 0 || fs_handle == nullptr) {
    *error_out = "FspFileSystemCreate failed";
    return false;
  }

  std::wstring mount_w;
  mount_w.reserve(mount_point.size());
  for (char ch : mount_point) {
    mount_w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
  }

  const unsigned long nt_status_mount = set_mount(fs_handle, mount_w.c_str());
  if (nt_status_mount != 0) {
    delete_fs(fs_handle);
    *error_out = "FspFileSystemSetMountPoint failed";
    return false;
  }

  const unsigned long nt_status_dispatcher = start_dispatcher(fs_handle, 0);
  if (nt_status_dispatcher != 0) {
    delete_fs(fs_handle);
    *error_out = "FspFileSystemStartDispatcher failed";
    return false;
  }

  file_system_handle_ = fs_handle;
  dispatcher_started_ = true;
#endif

  registration_active_ = true;
  *error_out = "";
  return true;
#else
  (void)mount_point;
  (void)callbacks;
  *error_out = "WinFsp native API support is disabled (build with JDRIVE64_ENABLE_WINFSP)";
  return false;
#endif
}

bool DefaultWinFspNativeApi::Unregister(std::string* error_out) {
  if (error_out == nullptr) {
    return false;
  }

#if defined(JDRIVE64_ENABLE_WINFSP)
  if (!registration_active_) {
    *error_out = "WinFsp native API is not registered";
    return false;
  }

  registration_active_ = false;

#if defined(_WIN32)
  if (dispatcher_started_ && proc_stop_dispatcher_ != nullptr && file_system_handle_ != nullptr) {
    auto stop_dispatcher = reinterpret_cast<FspFileSystemStopDispatcherFn>(proc_stop_dispatcher_);
    stop_dispatcher(file_system_handle_);
    dispatcher_started_ = false;
  }
  if (file_system_handle_ != nullptr && proc_delete_ != nullptr) {
    auto delete_fs = reinterpret_cast<FspFileSystemDeleteFn>(proc_delete_);
    delete_fs(file_system_handle_);
    file_system_handle_ = nullptr;
  }
#endif

  *error_out = "";
  return true;
#else
  *error_out = "WinFsp native API support is disabled (build with JDRIVE64_ENABLE_WINFSP)";
  return false;
#endif
}

}  // namespace jdrive64
