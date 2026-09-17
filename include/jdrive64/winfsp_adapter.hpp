#pragma once

#include <string>

#include "jdrive64/winfsp_callbacks.hpp"
#include "jdrive64/winfsp_filesystem.hpp"

namespace jdrive64 {

class WinFspAdapter {
 public:
  bool StartReadOnly(WinFspFilesystem* filesystem,
                     const std::string& image_path,
                     const std::string& mount_point);
  bool Stop(WinFspFilesystem* filesystem, const std::string& mount_point);

  bool IsCallbacksInitialized() const;
  const WinFspCallbacks& Callbacks() const;

  const std::string& LastError() const;

 private:
  std::string last_error_;
  WinFspCallbacks callbacks_;
};

}  // namespace jdrive64
