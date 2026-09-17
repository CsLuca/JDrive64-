#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "d64_test_utils.hpp"

namespace {

using jdrive64::tests::CreateGoldenCorpus;

struct CmdResult {
  int exit_code = -1;
  std::string output;
};

std::string Quote(const std::string& s) {
  return "\"" + s + "\"";
}

CmdResult Run(const std::string& cmd) {
  CmdResult r;
  std::array<char, 4096> buf{};

  const std::string wrapped =
      "cmd /C \"set PATH=C:\\msys64\\ucrt64\\bin;%PATH%&& " + cmd + " 2>&1\"";

  FILE* pipe = _popen(wrapped.c_str(), "r");
  if (pipe == nullptr) {
    r.output = "cannot spawn process";
    return r;
  }

  while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
    r.output += buf.data();
  }

  const int raw = _pclose(pipe);
  r.exit_code = raw;
  return r;
}

bool Check(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    return false;
  }
  return true;
}

bool Contains(const std::string& s, const std::string& sub) {
  return s.find(sub) != std::string::npos;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Need jdrive64 executable path\n";
    return 1;
  }

  const std::filesystem::path exe_path = argv[1];
  const auto root = std::filesystem::temp_directory_path() / "jdrive64_cli_smoke";
  const auto golden = CreateGoldenCorpus(root);

  bool ok = true;

  {
    const auto cmd = Quote(exe_path.string()) + " version";
    const auto r = Run(cmd);
    ok = ok && Check(r.exit_code == 0, "version exits 0");
    ok = ok && Check(Contains(r.output, "JDrive64 "), "version contains semantic version");
  }

  {
    const auto cmd = Quote(exe_path.string()) + " info " + Quote(golden.valid_small.string());
    const auto r = Run(cmd);
    ok = ok && Check(r.exit_code == 0, "info exits 0");
    ok = ok && Check(Contains(r.output, "Label"), "info contains Label");
    ok = ok && Check(Contains(r.output, "Capacity"), "info contains Capacity");
    ok = ok && Check(Contains(r.output, "Free"), "info contains Free");
  }

  {
    const auto cmd = Quote(exe_path.string()) + " ls " + Quote(golden.valid_small.string());
    const auto r = Run(cmd);
    ok = ok && Check(r.exit_code == 0, "ls exits 0");
    ok = ok && Check(Contains(r.output, "HELLO.PRG"), "ls contains HELLO.PRG");
  }

  {
    const auto out_dir = root / "extract_out";
    std::error_code ec;
    std::filesystem::remove_all(out_dir, ec);

    const auto cmd = Quote(exe_path.string()) + " extract " + Quote(golden.valid_small.string()) +
                     " " + Quote(out_dir.string());
    const auto r = Run(cmd);
    ok = ok && Check(r.exit_code == 0, "extract exits 0 for valid image");
    ok = ok && Check(std::filesystem::exists(out_dir / "HELLO.PRG"), "extract writes HELLO.PRG");

    std::ifstream in(out_dir / "HELLO.PRG", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ok = ok && Check(text == "HELLO", "extract payload is HELLO");
  }

  {
    const auto cmd = Quote(exe_path.string()) + " info " + Quote(golden.bad_size.string());
    const auto r = Run(cmd);
    ok = ok && Check(r.exit_code != 0, "info exits non-zero for invalid image");
  }

  {
    const auto mount_cmd = Quote(exe_path.string()) + " mount " + Quote(golden.valid_small.string()) +
                           " Z:";
    const auto mount_r = Run(mount_cmd);
    ok = ok && Check(mount_r.exit_code == 0, "mount exits 0");
    ok = ok && Check(Contains(mount_r.output, "Mounted"), "mount output contains Mounted");

    const auto mount_again_r = Run(mount_cmd);
    ok = ok && Check(mount_again_r.exit_code != 0, "mount fails when already mounted");

    const auto mount_kdrv_cmd = Quote(exe_path.string()) + " mount " + Quote(golden.valid_small.string()) +
                                " Y: --backend kdrv";
    const auto mount_kdrv_r = Run(mount_kdrv_cmd);
    ok = ok && Check(mount_kdrv_r.exit_code != 0, "mount with kdrv backend fails (not implemented)");
    ok = ok && Check(Contains(mount_kdrv_r.output, "Kernel backend not implemented"),
                     "mount with kdrv reports not implemented");

    const auto mounts_cmd = Quote(exe_path.string()) + " mounts";
    const auto mounts_r = Run(mounts_cmd);
    ok = ok && Check(mounts_r.exit_code == 0, "mounts exits 0");
    ok = ok && Check(Contains(mounts_r.output, "Z:"), "mounts contains drive letter");

    const auto dir_cmd = Quote(exe_path.string()) + " dir-mounted Z:";
    const auto dir_r = Run(dir_cmd);
    ok = ok && Check(dir_r.exit_code == 0, "dir-mounted exits 0");
    ok = ok && Check(Contains(dir_r.output, "HELLO.PRG"), "dir-mounted contains HELLO.PRG");

    const auto read_cmd = Quote(exe_path.string()) + " read-mounted Z: HELLO.PRG";
    const auto read_r = Run(read_cmd);
    ok = ok && Check(read_r.exit_code == 0, "read-mounted exits 0");
    ok = ok && Check(Contains(read_r.output, "HELLO"), "read-mounted returns HELLO");

    const auto volume_cmd = Quote(exe_path.string()) + " volume-mounted Z:";
    const auto volume_r = Run(volume_cmd);
    ok = ok && Check(volume_r.exit_code == 0, "volume-mounted exits 0");
    ok = ok && Check(Contains(volume_r.output, "Label:"), "volume-mounted contains Label");
    ok = ok && Check(Contains(volume_r.output, "Capacity:"), "volume-mounted contains Capacity");

    const auto stats_cmd = Quote(exe_path.string()) + " stats-mounted Z:";
    const auto stats_r = Run(stats_cmd);
    ok = ok && Check(stats_r.exit_code == 0, "stats-mounted exits 0");
    ok = ok && Check(Contains(stats_r.output, "SectorCache"), "stats-mounted contains SectorCache");
    ok = ok && Check(Contains(stats_r.output, "ReadOps="), "stats-mounted contains ReadOps");

    const auto check_cmd = Quote(exe_path.string()) + " check-mounted Z:";
    const auto check_r = Run(check_cmd);
    ok = ok && Check(check_r.exit_code == 0, "check-mounted exits 0");
    ok = ok && Check(Contains(check_r.output, "Status: OK"), "check-mounted reports OK status");
    ok = ok && Check(Contains(check_r.output, "Mount: Z:"), "check-mounted reports mount point");

    const auto preflight_cmd = Quote(exe_path.string()) + " winfsp-preflight " +
                               Quote(golden.valid_small.string()) + " Y:";
    const auto preflight_r = Run(preflight_cmd);
    ok = ok && Check(preflight_r.exit_code != 0, "winfsp-preflight fails without WinFsp runtime support");

    const auto unmount_cmd = Quote(exe_path.string()) + " unmount Z:";
    const auto unmount_r = Run(unmount_cmd);
    ok = ok && Check(unmount_r.exit_code == 0, "unmount exits 0");
    ok = ok && Check(Contains(unmount_r.output, "Unmounted Z:"), "unmount output contains Unmounted");

    const auto unmount_again_r = Run(unmount_cmd);
    ok = ok && Check(unmount_again_r.exit_code != 0, "unmount fails when not mounted");

    const auto mounts_after_unmount_r = Run(mounts_cmd);
    ok = ok && Check(mounts_after_unmount_r.exit_code == 0, "mounts exits 0 after unmount");
    ok = ok && Check(!Contains(mounts_after_unmount_r.output, "Z:"),
                     "mounts no longer contains drive letter after unmount");

    const auto check_after_unmount_r = Run(check_cmd);
    ok = ok && Check(check_after_unmount_r.exit_code != 0,
                     "check-mounted fails after unmount");
  }

  if (!ok) {
    return 1;
  }

  std::cout << "CLI smoke tests passed\n";
  return 0;
}
