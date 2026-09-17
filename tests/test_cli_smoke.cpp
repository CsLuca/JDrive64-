#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <cstdlib>

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

#if defined(_WIN32)
  _putenv_s("JDRIVE64_TELEMETRY_JSONL", (root / "telemetry.jsonl").string().c_str());
  _putenv_s("JDRIVE64_TELEMETRY_MAX_BYTES", "128");
#else
  setenv("JDRIVE64_TELEMETRY_JSONL", (root / "telemetry.jsonl").string().c_str(), 1);
  setenv("JDRIVE64_TELEMETRY_MAX_BYTES", "128", 1);
#endif

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
    const auto backend_diag_cmd = Quote(exe_path.string()) + " backend-diag " +
                                  Quote(golden.valid_small.string()) + " --backend kdrv";
    const auto backend_diag_r = Run(backend_diag_cmd);
    ok = ok && Check(backend_diag_r.exit_code == 0, "backend-diag exits 0 for kdrv");
    ok = ok && Check(Contains(backend_diag_r.output, "RequestedBackend: kdrv"),
                     "backend-diag reports requested backend");
    ok = ok && Check(Contains(backend_diag_r.output, "Backend=kdrv"),
                     "backend-diag reports kdrv diagnostics");

    const auto backend_diag_json_cmd = Quote(exe_path.string()) + " backend-diag " +
                                       Quote(golden.valid_small.string()) + " --backend kdrv --json";
    const auto backend_diag_json_r = Run(backend_diag_json_cmd);
    ok = ok && Check(backend_diag_json_r.exit_code == 0, "backend-diag --json exits 0 for kdrv");
    ok = ok && Check(Contains(backend_diag_json_r.output, "\"requested_backend\": \"kdrv\""),
                     "backend-diag --json reports requested backend");

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
    ok = ok && Check(Contains(mount_kdrv_r.output, "not implemented"),
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

    const auto backend_diag_mounted_cmd = Quote(exe_path.string()) + " backend-diag-mounted Z:";
    const auto backend_diag_mounted_r = Run(backend_diag_mounted_cmd);
    ok = ok && Check(backend_diag_mounted_r.exit_code == 0, "backend-diag-mounted exits 0");
    ok = ok && Check(Contains(backend_diag_mounted_r.output, "Backend: winfsp"),
                     "backend-diag-mounted reports persisted backend");

    const auto backend_diag_mounted_json_cmd =
        Quote(exe_path.string()) + " backend-diag-mounted Z: --json";
    const auto backend_diag_mounted_json_r = Run(backend_diag_mounted_json_cmd);
    ok = ok && Check(backend_diag_mounted_json_r.exit_code == 0,
                     "backend-diag-mounted --json exits 0");
    ok = ok && Check(Contains(backend_diag_mounted_json_r.output, "\"backend\": \"winfsp\""),
                     "backend-diag-mounted --json reports persisted backend");
    ok = ok && Check(Contains(backend_diag_mounted_json_r.output, "\"telemetry_jsonl\":"),
                     "backend-diag-mounted --json reports telemetry path field");

    const auto telemetry_dump_cmd = Quote(exe_path.string()) + " telemetry-dump-mounted Z:";
    const auto telemetry_dump_r = Run(telemetry_dump_cmd);
    ok = ok && Check(telemetry_dump_r.exit_code == 0, "telemetry-dump-mounted exits 0");

    const auto telemetry_dump_filter_cmd = Quote(exe_path.string()) +
                                           " telemetry-dump-mounted Z: --event send.loopback --tail 1";
    const auto telemetry_dump_filter_r = Run(telemetry_dump_filter_cmd);
    ok = ok && Check(telemetry_dump_filter_r.exit_code == 0,
                     "telemetry-dump-mounted with filters exits 0");

    const auto telemetry_dump_json_cmd =
        Quote(exe_path.string()) + " telemetry-dump-mounted Z: --event send.loopback --offset 0 --limit 1 --json";
    const auto telemetry_dump_json_r = Run(telemetry_dump_json_cmd);
    ok = ok && Check(telemetry_dump_json_r.exit_code == 0,
                     "telemetry-dump-mounted --json with pagination exits 0");
    ok = ok && Check(Contains(telemetry_dump_json_r.output, "\"entries\":"),
                     "telemetry-dump-mounted --json reports entries field");

    const auto telemetry_dump_dsl_cmd = Quote(exe_path.string()) +
                                        " telemetry-dump-mounted Z: --event-prefix send. --event-contains loop --exclude-event connect.loopback --success true --tail 2";
    const auto telemetry_dump_dsl_r = Run(telemetry_dump_dsl_cmd);
    ok = ok && Check(telemetry_dump_dsl_r.exit_code == 0,
                     "telemetry-dump-mounted DSL filters exit 0");

    const auto telemetry_dump_bundle_cmd = Quote(exe_path.string()) +
                                           " telemetry-dump-mounted Z: --event-prefix send. --selector-mode any --bundle";
    const auto telemetry_dump_bundle_r = Run(telemetry_dump_bundle_cmd);
    ok = ok && Check(telemetry_dump_bundle_r.exit_code == 0,
                     "telemetry-dump-mounted --bundle exits 0");
    ok = ok && Check(Contains(telemetry_dump_bundle_r.output, "\"schema_version\": \"telemetry-query.v1\""),
                     "telemetry-dump-mounted --bundle reports schema version");
    ok = ok && Check(Contains(telemetry_dump_bundle_r.output, "\"schema_policy\":"),
                     "telemetry-dump-mounted --bundle reports schema policy");
    ok = ok && Check(Contains(telemetry_dump_bundle_r.output, "\"versioning\": \"semver-compatible\""),
                     "telemetry-dump-mounted --bundle reports schema versioning policy");
    ok = ok && Check(Contains(telemetry_dump_bundle_r.output, "\"query\":"),
                     "telemetry-dump-mounted --bundle reports query field");
    ok = ok && Check(Contains(telemetry_dump_bundle_r.output, "\"selector_mode\": \"any\""),
                     "telemetry-dump-mounted --bundle reports selector mode");
    ok = ok && Check(Contains(telemetry_dump_bundle_r.output, "\"stats\":"),
                     "telemetry-dump-mounted --bundle reports stats field");
    ok = ok && Check(Contains(telemetry_dump_bundle_r.output, "\"files_scanned\":"),
                     "telemetry-dump-mounted --bundle reports files_scanned field");

    const auto telemetry_dump_norm_cmd = Quote(exe_path.string()) +
                                         " telemetry-dump-mounted Z: --exclude-event connect.loopback --selector-mode any --bundle";
    const auto telemetry_dump_norm_r = Run(telemetry_dump_norm_cmd);
    ok = ok && Check(telemetry_dump_norm_r.exit_code == 0,
                     "telemetry-dump-mounted normalized query exits 0");
    ok = ok && Check(Contains(telemetry_dump_norm_r.output, "\"selector_mode\": \"all\""),
                     "telemetry-dump-mounted normalizes selector mode without positive selectors");

    const auto telemetry_dump_where_cmd = Quote(exe_path.string()) +
                                          " telemetry-dump-mounted Z: --where \"event_prefix==send. AND success==true\" --bundle";
    const auto telemetry_dump_where_r = Run(telemetry_dump_where_cmd);
    ok = ok && Check(telemetry_dump_where_r.exit_code == 0,
                     "telemetry-dump-mounted --where exits 0");
    ok = ok && Check(Contains(telemetry_dump_where_r.output, "\"where\": \"event_prefix==send. AND success==true\""),
                     "telemetry-dump-mounted --where reports normalized expression");

    const auto telemetry_dump_where_grouped_cmd = Quote(exe_path.string()) +
                                                  " telemetry-dump-mounted Z: --where \"( event_prefix==send. AND success==true ) OR event==disconnect.loopback\" --bundle";
    const auto telemetry_dump_where_grouped_r = Run(telemetry_dump_where_grouped_cmd);
    ok = ok && Check(telemetry_dump_where_grouped_r.exit_code == 0,
                     "telemetry-dump-mounted grouped --where exits 0");
    ok = ok && Check(Contains(telemetry_dump_where_grouped_r.output, "\"where\":"),
                     "telemetry-dump-mounted grouped --where reports where field");
    ok = ok && Check(Contains(telemetry_dump_where_grouped_r.output,
                              "event_prefix==send. AND success==true"),
                     "telemetry-dump-mounted grouped --where keeps grouped conjunction");
    ok = ok && Check(Contains(telemetry_dump_where_grouped_r.output,
                              "OR event==disconnect.loopback"),
                     "telemetry-dump-mounted grouped --where keeps OR branch");

    const auto telemetry_dump_where_quoted_cmd = Quote(exe_path.string()) +
                                                 " telemetry-dump-mounted Z: --where \"event==\\\"send.loopback\\\" AND success==\\\"true\\\"\" --bundle";
    const auto telemetry_dump_where_quoted_r = Run(telemetry_dump_where_quoted_cmd);
    ok = ok && Check(telemetry_dump_where_quoted_r.exit_code == 0,
                     "telemetry-dump-mounted quoted --where exits 0");
    ok = ok && Check(Contains(telemetry_dump_where_quoted_r.output,
                              "\"where\": \"event==\\\"send.loopback\\\" AND success==true\""),
                     "telemetry-dump-mounted quoted --where normalizes quoted tokens");

    const auto telemetry_dump_where_not_cmd = Quote(exe_path.string()) +
                                              " telemetry-dump-mounted Z: --where \"NOT event==connect.loopback AND success==true\" --bundle";
    const auto telemetry_dump_where_not_r = Run(telemetry_dump_where_not_cmd);
    ok = ok && Check(telemetry_dump_where_not_r.exit_code == 0,
                     "telemetry-dump-mounted NOT --where exits 0");
    ok = ok && Check(Contains(telemetry_dump_where_not_r.output,
                              "\"where\": \"NOT event==connect.loopback AND success==true\""),
                     "telemetry-dump-mounted NOT --where preserves unary not expression");

    const auto telemetry_dump_where_suffix_cmd = Quote(exe_path.string()) +
                                                 " telemetry-dump-mounted Z: --where \"event_suffix==loopback AND success==true\" --bundle";
    const auto telemetry_dump_where_suffix_r = Run(telemetry_dump_where_suffix_cmd);
    ok = ok && Check(telemetry_dump_where_suffix_r.exit_code == 0,
                     "telemetry-dump-mounted suffix --where exits 0");
    ok = ok && Check(Contains(telemetry_dump_where_suffix_r.output,
                              "\"where\": \"event_suffix==loopback AND success==true\""),
                     "telemetry-dump-mounted suffix --where preserves suffix expression");

    const auto telemetry_dump_explain_cmd = Quote(exe_path.string()) +
                                            " telemetry-dump-mounted Z: --where \"event_suffix==loopback AND success==true\" --explain";
    const auto telemetry_dump_explain_r = Run(telemetry_dump_explain_cmd);
    ok = ok && Check(telemetry_dump_explain_r.exit_code == 0,
                     "telemetry-dump-mounted --explain exits 0");
    ok = ok && Check(Contains(telemetry_dump_explain_r.output, "\"query_plan\":"),
                     "telemetry-dump-mounted --explain reports query_plan");
    ok = ok && Check(Contains(telemetry_dump_explain_r.output, "\"where_rpn_tokens\":"),
                     "telemetry-dump-mounted --explain reports where_rpn_tokens");

    const auto telemetry_dump_where_detail_cmd = Quote(exe_path.string()) +
                                                 " telemetry-dump-mounted Z: --where \"detail_contains==success\" --bundle";
    const auto telemetry_dump_where_detail_r = Run(telemetry_dump_where_detail_cmd);
    ok = ok && Check(telemetry_dump_where_detail_r.exit_code == 0,
                     "telemetry-dump-mounted detail --where exits 0");
    ok = ok && Check(Contains(telemetry_dump_where_detail_r.output,
                              "\"where\": \"detail_contains==success\""),
                     "telemetry-dump-mounted detail --where preserves detail predicate");

    const auto telemetry_dump_where_detail_prefix_cmd = Quote(exe_path.string()) +
                                                        " telemetry-dump-mounted Z: --where \"detail_prefix==send\" --bundle";
    const auto telemetry_dump_where_detail_prefix_r = Run(telemetry_dump_where_detail_prefix_cmd);
    ok = ok && Check(telemetry_dump_where_detail_prefix_r.exit_code == 0,
                     "telemetry-dump-mounted detail prefix --where exits 0");
    ok = ok && Check(Contains(telemetry_dump_where_detail_prefix_r.output,
                              "\"where\": \"detail_prefix==send\""),
                     "telemetry-dump-mounted detail prefix --where preserves predicate");

    const auto telemetry_clear_cmd = Quote(exe_path.string()) + " telemetry-clear-mounted Z:";
    const auto telemetry_clear_r = Run(telemetry_clear_cmd);
    ok = ok && Check(telemetry_clear_r.exit_code == 0, "telemetry-clear-mounted exits 0");
    ok = ok && Check(Contains(telemetry_clear_r.output, "Cleared telemetry JSONL"),
                     "telemetry-clear-mounted reports clear message");

    const auto telemetry_list_cmd = Quote(exe_path.string()) + " telemetry-list-mounted Z:";
    const auto telemetry_list_r = Run(telemetry_list_cmd);
    ok = ok && Check(telemetry_list_r.exit_code == 0, "telemetry-list-mounted exits 0");
    ok = ok && Check(Contains(telemetry_list_r.output, "Telemetry files for Z:"),
                     "telemetry-list-mounted reports header");

    const auto telemetry_stats_cmd = Quote(exe_path.string()) + " telemetry-stats-mounted Z:";
    const auto telemetry_stats_r = Run(telemetry_stats_cmd);
    ok = ok && Check(telemetry_stats_r.exit_code == 0, "telemetry-stats-mounted exits 0");
    ok = ok && Check(Contains(telemetry_stats_r.output, "Total events:"),
                     "telemetry-stats-mounted reports totals");

    const auto telemetry_stats_json_cmd = Quote(exe_path.string()) + " telemetry-stats-mounted Z: --json";
    const auto telemetry_stats_json_r = Run(telemetry_stats_json_cmd);
    ok = ok && Check(telemetry_stats_json_r.exit_code == 0,
                     "telemetry-stats-mounted --json exits 0");
    ok = ok && Check(Contains(telemetry_stats_json_r.output, "\"events\":"),
                     "telemetry-stats-mounted --json reports events array");

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
