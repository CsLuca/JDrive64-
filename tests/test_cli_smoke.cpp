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

  FILE* pipe = _popen((cmd + " 2>&1").c_str(), "r");
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
    const auto cmd = Quote(exe_path.string()) + " info " + Quote(golden.valid_small.string());
    const auto r = Run(cmd);
    ok = ok && Check(r.exit_code == 0, "info exits 0");
    ok = ok && Check(Contains(r.output, "Disk Name"), "info contains Disk Name");
    ok = ok && Check(Contains(r.output, "Blocks"), "info contains Blocks");
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

  if (!ok) {
    return 1;
  }

  std::cout << "CLI smoke tests passed\n";
  return 0;
}
