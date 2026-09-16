#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "d64_test_utils.hpp"

#include "jdrive64/d64_image_editor.hpp"
#include "jdrive64/disk_image_session.hpp"

namespace {

using jdrive64::D64ImageEditor;
using jdrive64::DiskImageSession;
using jdrive64::tests::CreateGoldenCorpus;

bool Check(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    return false;
  }
  return true;
}

bool TestWriteFlow(const std::filesystem::path& base_image, const std::filesystem::path& work_root) {
  const auto image = work_root / "write_flow.d64";
  std::error_code ec;
  std::filesystem::create_directories(work_root, ec);
  std::filesystem::copy_file(base_image, image, std::filesystem::copy_options::overwrite_existing, ec);

  const auto host_file = work_root / "host.bin";
  {
    std::ofstream out(host_file, std::ios::binary | std::ios::trunc);
    out << "PAYLOAD";
  }

  D64ImageEditor editor;
  if (!Check(editor.Open(image.string()), "editor open")) {
    return false;
  }

  if (!Check(editor.AddFile(host_file.string(), "NEWFILE.PRG"), "add file")) {
    return false;
  }

  DiskImageSession session_after_add;
  if (!Check(session_after_add.Open(image.string()), "session open after add")) {
    return false;
  }

  std::vector<std::uint8_t> data;
  if (!Check(session_after_add.ReadFileByWindowsName("NEWFILE.PRG", &data), "read added file")) {
    return false;
  }
  if (!Check(std::string(data.begin(), data.end()) == "PAYLOAD", "added file payload")) {
    return false;
  }

  if (!Check(editor.RenameFile("NEWFILE.PRG", "RENAMED.SEQ"), "rename file")) {
    return false;
  }

  DiskImageSession session_after_rename;
  if (!Check(session_after_rename.Open(image.string()), "session open after rename")) {
    return false;
  }
  if (!Check(!session_after_rename.ReadFileByWindowsName("NEWFILE.PRG", &data),
             "old name missing after rename")) {
    return false;
  }
  if (!Check(session_after_rename.ReadFileByWindowsName("RENAMED.SEQ", &data),
             "new name readable after rename")) {
    return false;
  }

  if (!Check(editor.DeleteFile("RENAMED.SEQ"), "delete file")) {
    return false;
  }

  DiskImageSession session_after_delete;
  if (!Check(session_after_delete.Open(image.string()), "session open after delete")) {
    return false;
  }
  if (!Check(!session_after_delete.ReadFileByWindowsName("RENAMED.SEQ", &data),
             "deleted file missing")) {
    return false;
  }

  return true;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "jdrive64_write_tests";
  const auto golden = CreateGoldenCorpus(root / "golden");

  const bool ok = TestWriteFlow(golden.valid_small, root / "work");
  if (!ok) {
    return 1;
  }

  std::cout << "Write editor tests passed\n";
  return 0;
}
