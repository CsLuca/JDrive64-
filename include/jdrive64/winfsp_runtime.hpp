#pragma once

#include <string>

#include "jdrive64/winfsp_adapter.hpp"
#include "jdrive64/winfsp_filesystem.hpp"

namespace jdrive64 {

class WinFspRuntime {
 public:
  bool StartReadOnly(const std::string& image_path, const std::string& mount_point);
  bool Stop();

  bool IsRunning() const;
  const std::string& LastError() const;

 private:
 bool running_ = false;
  std::string mount_point_;
  std::string last_error_;
  WinFspAdapter adapter_;
  WinFspFilesystem filesystem_;
};

}  // namespace jdrive64
