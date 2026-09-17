#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <algorithm>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "jdrive64/d64_image_editor.hpp"
#include "jdrive64/disk_image_session.hpp"
#include "jdrive64/mount_backend.hpp"
#include "jdrive64/winfsp_filesystem.hpp"
#include "jdrive64/winfsp_runtime.hpp"

namespace {

using jdrive64::DiskImageSession;
using jdrive64::WinFspFilesystem;
using jdrive64::D64ImageEditor;
using jdrive64::WinFspRuntime;

constexpr std::uint32_t kD64BlockSizeBytes = 256;
constexpr std::uint32_t kD64TotalBlocks = 664;

std::filesystem::path MountStateRoot() {
  return std::filesystem::temp_directory_path() / "jdrive64_mounts";
}

std::string NormalizeMountPoint(std::string mount_point) {
  for (char& c : mount_point) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }
  return mount_point;
}

bool IsValidMountPoint(const std::string& mount_point) {
  if (mount_point.size() != 2 || mount_point[1] != ':') {
    return false;
  }
  return mount_point[0] >= 'A' && mount_point[0] <= 'Z';
}

std::filesystem::path MountStateFile(const std::string& mount_point) {
  const std::string file_name(1, mount_point[0]);
  return MountStateRoot() / (file_name + ".state");
}

struct MountState {
  std::string mount_point;
  std::string image_path;
  std::vector<std::string> files;
};

bool ParsePrefixedLine(const std::string& line, const std::string& prefix, std::string* value_out) {
  if (!line.starts_with(prefix) || value_out == nullptr) {
    return false;
  }
  *value_out = line.substr(prefix.size());
  return true;
}

bool LoadMountState(const std::filesystem::path& state_file, MountState* state, std::string* error_out) {
  if (state == nullptr) {
    if (error_out != nullptr) {
      *error_out = "Invalid mount state output";
    }
    return false;
  }

  std::ifstream in(state_file, std::ios::binary);
  if (!in) {
    if (error_out != nullptr) {
      *error_out = "Cannot open mount state file";
    }
    return false;
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) {
    lines.push_back(line);
  }

  if (lines.size() < 3) {
    if (error_out != nullptr) {
      *error_out = "Mount state is incomplete";
    }
    return false;
  }

  if (lines[0] != "VERSION=1") {
    if (error_out != nullptr) {
      *error_out = "Unsupported mount state version";
    }
    return false;
  }

  MountState parsed;
  if (!ParsePrefixedLine(lines[1], "MOUNT_POINT=", &parsed.mount_point) ||
      !ParsePrefixedLine(lines[2], "IMAGE_PATH=", &parsed.image_path)) {
    if (error_out != nullptr) {
      *error_out = "Mount state header is invalid";
    }
    return false;
  }

  if (!IsValidMountPoint(parsed.mount_point) || parsed.image_path.empty()) {
    if (error_out != nullptr) {
      *error_out = "Mount state values are invalid";
    }
    return false;
  }

  for (std::size_t i = 3; i < lines.size(); ++i) {
    std::string file;
    if (!ParsePrefixedLine(lines[i], "FILE=", &file)) {
      if (error_out != nullptr) {
        *error_out = "Mount state file list is invalid";
      }
      return false;
    }
    if (!file.empty()) {
      parsed.files.push_back(file);
    }
  }

  *state = std::move(parsed);
  return true;
}

bool SaveMountState(const std::filesystem::path& state_file, const MountState& state, std::string* error_out) {
  const auto temp_file = state_file.string() + ".tmp";
  {
    std::ofstream out(temp_file, std::ios::binary | std::ios::trunc);
    if (!out) {
      if (error_out != nullptr) {
        *error_out = "Cannot write mount state temp file";
      }
      return false;
    }

    out << "VERSION=1\n";
    out << "MOUNT_POINT=" << state.mount_point << "\n";
    out << "IMAGE_PATH=" << state.image_path << "\n";
    for (const auto& file : state.files) {
      out << "FILE=" << file << "\n";
    }

    if (!out) {
      if (error_out != nullptr) {
        *error_out = "Failed while writing mount state";
      }
      return false;
    }
  }

  std::error_code ec;
  std::filesystem::rename(temp_file, state_file, ec);
  if (ec) {
    if (error_out != nullptr) {
      *error_out = "Cannot finalize mount state file";
    }
    std::filesystem::remove(temp_file, ec);
    return false;
  }

  return true;
}

std::filesystem::path DetectRepoRootFromExecutable() {
  std::error_code ec;
  const auto exe_path = std::filesystem::canonical(std::filesystem::path("."), ec);
  if (!ec) {
    auto probe = exe_path;
    while (!probe.empty()) {
      if (std::filesystem::exists(probe / "VERSION") && std::filesystem::exists(probe / "CMakeLists.txt")) {
        return probe;
      }
      const auto parent = probe.parent_path();
      if (parent == probe) {
        break;
      }
      probe = parent;
    }
  }

  return {};
}

std::string TrimAsciiWhitespace(std::string value) {
  auto is_space = [](unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
  };

  while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

bool ReadProjectVersion(std::string* version_out, std::string* error_out) {
  if (version_out == nullptr || error_out == nullptr) {
    return false;
  }

  const auto root = DetectRepoRootFromExecutable();
  if (root.empty()) {
    *error_out = "Cannot locate repository root";
    return false;
  }

  const auto version_path = root / "VERSION";
  std::ifstream in(version_path, std::ios::binary);
  if (!in) {
    *error_out = "Cannot open VERSION file";
    return false;
  }

  std::string line;
  if (!std::getline(in, line)) {
    *error_out = "VERSION file is empty";
    return false;
  }

  line = TrimAsciiWhitespace(line);
  if (line.empty()) {
    *error_out = "VERSION value is empty";
    return false;
  }

  *version_out = std::move(line);
  return true;
}

bool PrepareMountedFilesystem(std::string mount_point,
                             WinFspFilesystem* fs_out,
                             MountState* state_out,
                             std::string* normalized_mount_out,
                             std::string* error_out) {
  if (fs_out == nullptr || state_out == nullptr || error_out == nullptr) {
    return false;
  }

  mount_point = NormalizeMountPoint(std::move(mount_point));
  if (!IsValidMountPoint(mount_point)) {
    *error_out = "invalid mount point, expected format X:";
    return false;
  }

  const auto state_file = MountStateFile(mount_point);
  if (!std::filesystem::exists(state_file)) {
    *error_out = "mount point is not mounted: " + mount_point;
    return false;
  }

  MountState state;
  std::string load_error;
  if (!LoadMountState(state_file, &state, &load_error)) {
    *error_out = "invalid mount state: " + load_error;
    return false;
  }
  if (state.mount_point != mount_point) {
    *error_out = "mount state mismatch for " + mount_point;
    return false;
  }

  if (!fs_out->MountReadOnly(state.image_path, mount_point)) {
    *error_out = fs_out->LastError();
    return false;
  }

  *state_out = std::move(state);
  if (normalized_mount_out != nullptr) {
    *normalized_mount_out = std::move(mount_point);
  }
  return true;
}

int CmdMountWithBackend(const std::string& image_path,
                       std::string mount_point,
                       const std::string& backend_name);

void PrintUsage() {
  std::cout << "JDrive64 CLI\n"
            << "Usage:\n"
            << "  jdrive64 info <image.d64>\n"
            << "  jdrive64 version\n"
            << "  jdrive64 ls <image.d64>\n"
            << "  jdrive64 extract <image.d64> [output_dir]\n"
            << "  jdrive64 mount <image.d64> <drive_letter:>\n"
            << "  jdrive64 mount <image.d64> <drive_letter:> --backend <winfsp|kdrv>\n"
            << "  jdrive64 mounts\n"
            << "  jdrive64 unmount <drive_letter:>\n"
            << "  jdrive64 dir-mounted <drive_letter:>\n"
            << "  jdrive64 read-mounted <drive_letter:> <name.ext>\n"
            << "  jdrive64 volume-mounted <drive_letter:>\n"
            << "  jdrive64 stats-mounted <drive_letter:>\n"
            << "  jdrive64 check-mounted <drive_letter:>\n"
            << "  jdrive64 winfsp-preflight <image.d64> <drive_letter:>\n"
            << "  jdrive64 write-add <image.d64> <host_file> <name.ext>\n"
            << "  jdrive64 write-del <image.d64> <name.ext>\n"
            << "  jdrive64 write-ren <image.d64> <old.ext> <new.ext>\n";
}

std::string SanitizeFilename(const std::string& base_name, const std::string& ext) {
  std::string out;
  out.reserve(base_name.size() + ext.size() + 1);

  const std::string invalid = "<>:\\|?*\"/";
  for (char c : base_name) {
    if (c < 32 || invalid.find(c) != std::string::npos) {
      out.push_back('_');
    } else {
      out.push_back(c);
    }
  }

  if (out.empty()) {
    out = "UNNAMED";
  }

  out += ".";
  out += ext;
  return out;
}

int CmdInfo(const std::string& image_path) {
  DiskImageSession session;
  if (!session.Open(image_path)) {
    std::cerr << "Error: " << session.LastError() << "\n";
    return 1;
  }

  const std::uint32_t free_blocks = session.Bam().FreeBlocks();
  const std::uint32_t used_blocks = kD64TotalBlocks - free_blocks;
  const std::uint64_t capacity_bytes =
      static_cast<std::uint64_t>(kD64TotalBlocks) * kD64BlockSizeBytes;
  const std::uint64_t free_bytes = static_cast<std::uint64_t>(free_blocks) * kD64BlockSizeBytes;
  const std::uint64_t used_bytes = static_cast<std::uint64_t>(used_blocks) * kD64BlockSizeBytes;

  std::cout << "Label      : " << session.Bam().DiskName() << "\n";
  std::cout << "Disk ID    : " << session.Bam().DiskId() << "\n";
  std::cout << "DOS Type   : " << session.Bam().DosType() << "\n";
  std::cout << "FileSystem : JDrive64\n";
  std::cout << "Block Size : " << kD64BlockSizeBytes << " bytes\n";
  std::cout << "Capacity   : " << kD64TotalBlocks << " blocks (" << capacity_bytes << " bytes)\n";
  std::cout << "Used       : " << used_blocks << " blocks (" << used_bytes << " bytes)\n";
  std::cout << "Free       : " << free_blocks << " blocks (" << free_bytes << " bytes)\n";
  return 0;
}

int CmdVersion() {
  std::string version;
  std::string error;
  if (!ReadProjectVersion(&version, &error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  std::cout << "JDrive64 " << version << "\n";
  return 0;
}

int CmdLs(const std::string& image_path) {
  DiskImageSession session;
  if (!session.Open(image_path)) {
    std::cerr << "Error: " << session.LastError() << "\n";
    return 1;
  }

  for (const auto& file : session.Catalog().Files()) {
    std::cout << file.windows_name << "  " << file.size_blocks << " blocks\n";
  }

  return 0;
}

int CmdExtract(const std::string& image_path, const std::string& output_dir_arg) {
  DiskImageSession session;
  if (!session.Open(image_path)) {
    std::cerr << "Error: " << session.LastError() << "\n";
    return 1;
  }

  std::filesystem::path output_dir;
  if (output_dir_arg.empty()) {
    const std::filesystem::path image(image_path);
    output_dir = image.stem().string() + "_extract";
  } else {
    output_dir = output_dir_arg;
  }

  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  if (ec) {
    std::cerr << "Error: cannot create output directory: " << output_dir.string() << "\n";
    return 1;
  }

  bool had_warnings = false;
  for (const auto& file : session.Catalog().Files()) {
    std::vector<std::uint8_t> file_data;
    if (!session.ReadFileByCatalogFile(file, &file_data)) {
      std::cerr << "Warning: cannot read " << file.display_name << ": " << session.LastError()
                << "\n";
      had_warnings = true;
      continue;
    }

    const auto file_name = SanitizeFilename(file.display_name, file.extension);
    const auto file_path = output_dir / file_name;
    std::ofstream out(file_path, std::ios::binary);
    if (!out) {
      std::cerr << "Warning: cannot write " << file_path.string() << "\n";
      had_warnings = true;
      continue;
    }

    if (!file_data.empty()) {
      out.write(reinterpret_cast<const char*>(file_data.data()),
                static_cast<std::streamsize>(file_data.size()));
    }

    if (!out) {
      std::cerr << "Warning: write failed for " << file_path.string() << "\n";
      had_warnings = true;
      continue;
    }

    std::cout << file_name << "\n";
  }

  return had_warnings ? 2 : 0;
}

int CmdMount(const std::string& image_path, std::string mount_point) {
  return CmdMountWithBackend(image_path, std::move(mount_point), "winfsp");
}

int CmdMountWithBackend(const std::string& image_path,
                       std::string mount_point,
                       const std::string& backend_name) {
  mount_point = NormalizeMountPoint(std::move(mount_point));
  if (!IsValidMountPoint(mount_point)) {
    std::cerr << "Error: invalid mount point, expected format X:\n";
    return 1;
  }

  std::error_code ec;
  std::filesystem::create_directories(MountStateRoot(), ec);
  if (ec) {
    std::cerr << "Error: cannot initialize mount state directory\n";
    return 1;
  }

  const auto state_file = MountStateFile(mount_point);
  if (std::filesystem::exists(state_file)) {
    std::cerr << "Error: mount point already marked as mounted: " << mount_point
              << " (unmount first)\n";
    return 1;
  }

  std::error_code fs_ec;
  auto resolved_image = std::filesystem::absolute(image_path, fs_ec);
  if (fs_ec) {
    resolved_image = image_path;
  }
  if (!std::filesystem::exists(resolved_image)) {
    std::cerr << "Error: image file does not exist: " << resolved_image.string() << "\n";
    return 1;
  }

  std::string normalized_backend;
  std::string backend_error;
  auto backend = jdrive64::CreateMountBackend(backend_name, &normalized_backend, &backend_error);
  if (backend == nullptr) {
    std::cerr << "Error: " << backend_error << "\n";
    return 1;
  }

  if (!backend->MountReadOnly(resolved_image.string(), mount_point)) {
    std::cerr << "Error: " << backend->LastError() << "\n";
    return 1;
  }

  if (!backend->HealthCheck()) {
    std::cerr << "Error: backend health check failed" << "\n";
    return 1;
  }

  MountState state;
  state.mount_point = mount_point;
  state.image_path = resolved_image.string();
  state.files = backend->ReadDirectory();

  std::string save_error;
  if (!SaveMountState(state_file, state, &save_error)) {
    std::cerr << "Error: cannot persist mount state: " << save_error << "\n";
    return 1;
  }

  std::cout << "Mounted " << resolved_image.string() << " on " << mount_point
            << " (read-only, backend=" << normalized_backend << ")\n";
  const auto info = backend->GetVolumeInfoText();
  if (!info.empty()) {
    std::cout << info << "\n";
  }
  return 0;
}

int CmdMounts() {
  std::error_code ec;
  std::filesystem::create_directories(MountStateRoot(), ec);
  if (ec) {
    std::cerr << "Error: cannot access mount state directory\n";
    return 1;
  }

  std::vector<std::string> lines;
  for (const auto& entry : std::filesystem::directory_iterator(MountStateRoot(), ec)) {
    if (ec) {
      std::cerr << "Error: cannot enumerate mount state directory\n";
      return 1;
    }
    if (!entry.is_regular_file()) {
      continue;
    }

    MountState state;
    std::string load_error;
    if (!LoadMountState(entry.path(), &state, &load_error)) {
      continue;
    }
    if (!IsValidMountPoint(state.mount_point)) {
      continue;
    }

    lines.push_back(state.mount_point + " -> " + state.image_path);
  }

  std::sort(lines.begin(), lines.end());
  for (const auto& line : lines) {
    std::cout << line << "\n";
  }
  return 0;
}

int CmdUnmount(std::string mount_point) {
  mount_point = NormalizeMountPoint(std::move(mount_point));
  if (!IsValidMountPoint(mount_point)) {
    std::cerr << "Error: invalid mount point, expected format X:\n";
    return 1;
  }

  const auto state_file = MountStateFile(mount_point);
  if (!std::filesystem::exists(state_file)) {
    std::cerr << "Error: mount point is not mounted: " << mount_point << "\n";
    return 1;
  }

  MountState state;
  std::string load_error;
  if (!LoadMountState(state_file, &state, &load_error)) {
    std::cerr << "Error: invalid mount state: " << load_error << "\n";
    return 1;
  }
  if (state.mount_point != mount_point) {
    std::cerr << "Error: mount state mismatch for " << mount_point << "\n";
    return 1;
  }

  std::error_code ec;
  std::filesystem::remove(state_file, ec);
  if (ec) {
    std::cerr << "Error: failed to remove mount state file\n";
    return 1;
  }

  std::cout << "Unmounted " << mount_point << "\n";
  return 0;
}

int CmdDirMounted(std::string mount_point) {
  std::string error;
  MountState state;
  WinFspFilesystem fs;
  if (!PrepareMountedFilesystem(std::move(mount_point), &fs, &state, nullptr, &error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  for (const auto& name : fs.ReadDirectory()) {
    std::cout << name << "\n";
  }

  return 0;
}

int CmdReadMounted(std::string mount_point, const std::string& windows_name) {
  std::string error;
  MountState state;
  WinFspFilesystem fs;
  if (!PrepareMountedFilesystem(std::move(mount_point), &fs, &state, nullptr, &error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  std::vector<std::uint8_t> data;
  if (!fs.ReadFileByWindowsName(windows_name, &data)) {
    std::cerr << "Error: " << fs.LastError() << "\n";
    return 1;
  }

  std::cout.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
  return 0;
}

int CmdVolumeMounted(std::string mount_point) {
  std::string error;
  MountState state;
  WinFspFilesystem fs;
  if (!PrepareMountedFilesystem(std::move(mount_point), &fs, &state, nullptr, &error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  std::cout << fs.GetVolumeInfoText() << "\n";
  return 0;
}

int CmdStatsMounted(std::string mount_point) {
  std::string error;
  MountState state;
  WinFspFilesystem fs;
  if (!PrepareMountedFilesystem(std::move(mount_point), &fs, &state, nullptr, &error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  std::cout << fs.GetRuntimeStatsText() << "\n";
  return 0;
}

int CmdCheckMounted(std::string mount_point) {
  std::string error;
  std::string normalized_mount;
  MountState state;
  WinFspFilesystem fs;
  if (!PrepareMountedFilesystem(std::move(mount_point), &fs, &state, &normalized_mount, &error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  WinFspFilesystem::VolumeInfo volume;
  if (!fs.GetVolumeInfo(&volume)) {
    std::cerr << "Error: " << fs.LastError() << "\n";
    return 1;
  }

  const auto names = fs.ReadDirectory();

  std::cout << "Mount: " << normalized_mount << "\n";
  std::cout << "Image: " << state.image_path << "\n";
  std::cout << "Status: OK\n";
  std::cout << "Files: " << names.size() << "\n";
  std::cout << "Volume: " << volume.label << " (" << volume.free_blocks << "/"
            << volume.capacity_blocks << " free blocks)\n";
  return 0;
}

int CmdWinfspPreflight(const std::string& image_path, std::string mount_point) {
  mount_point = NormalizeMountPoint(std::move(mount_point));
  if (!IsValidMountPoint(mount_point)) {
    std::cerr << "Error: invalid mount point, expected format X:\n";
    return 1;
  }

  std::error_code ec;
  const auto resolved_image = std::filesystem::absolute(image_path, ec);
  if (ec || !std::filesystem::exists(resolved_image)) {
    std::cerr << "Error: image file does not exist: " << image_path << "\n";
    return 1;
  }

  WinFspRuntime runtime;
  if (!runtime.StartReadOnly(resolved_image.string(), mount_point)) {
    std::cerr << "Error: " << runtime.LastError() << "\n";
    return 1;
  }

  if (!runtime.Stop()) {
    std::cerr << "Error: " << runtime.LastError() << "\n";
    return 1;
  }

  std::cout << "WinFsp preflight OK for " << mount_point << "\n";
  return 0;
}

int CmdWriteAdd(const std::string& image_path, const std::string& host_file, const std::string& windows_name) {
  D64ImageEditor editor;
  if (!editor.Open(image_path)) {
    std::cerr << "Error: " << editor.LastError() << "\n";
    return 1;
  }
  if (!editor.AddFile(host_file, windows_name)) {
    std::cerr << "Error: " << editor.LastError() << "\n";
    return 1;
  }
  std::cout << "Added " << windows_name << "\n";
  return 0;
}

int CmdWriteDel(const std::string& image_path, const std::string& windows_name) {
  D64ImageEditor editor;
  if (!editor.Open(image_path)) {
    std::cerr << "Error: " << editor.LastError() << "\n";
    return 1;
  }
  if (!editor.DeleteFile(windows_name)) {
    std::cerr << "Error: " << editor.LastError() << "\n";
    return 1;
  }
  std::cout << "Deleted " << windows_name << "\n";
  return 0;
}

int CmdWriteRen(const std::string& image_path,
                const std::string& old_windows_name,
                const std::string& new_windows_name) {
  D64ImageEditor editor;
  if (!editor.Open(image_path)) {
    std::cerr << "Error: " << editor.LastError() << "\n";
    return 1;
  }
  if (!editor.RenameFile(old_windows_name, new_windows_name)) {
    std::cerr << "Error: " << editor.LastError() << "\n";
    return 1;
  }
  std::cout << "Renamed " << old_windows_name << " -> " << new_windows_name << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 1;
  }

  const std::string command = argv[1];

  if (command == "version") {
    return CmdVersion();
  }

  if (command == "unmount") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    return CmdUnmount(argv[2]);
  }

  if (command == "mounts") {
    return CmdMounts();
  }

  if (command == "dir-mounted") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    return CmdDirMounted(argv[2]);
  }

  if (command == "read-mounted") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    return CmdReadMounted(argv[2], argv[3]);
  }

  if (command == "volume-mounted") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    return CmdVolumeMounted(argv[2]);
  }

  if (command == "stats-mounted") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    return CmdStatsMounted(argv[2]);
  }

  if (command == "check-mounted") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    return CmdCheckMounted(argv[2]);
  }

  if (command == "write-add") {
    if (argc < 5) {
      PrintUsage();
      return 1;
    }
    return CmdWriteAdd(argv[2], argv[3], argv[4]);
  }

  if (command == "write-del") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    return CmdWriteDel(argv[2], argv[3]);
  }

  if (command == "write-ren") {
    if (argc < 5) {
      PrintUsage();
      return 1;
    }
    return CmdWriteRen(argv[2], argv[3], argv[4]);
  }

  if (command == "winfsp-preflight") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    return CmdWinfspPreflight(argv[2], argv[3]);
  }

  if (argc < 3) {
    PrintUsage();
    return 1;
  }

  const std::string image_path = argv[2];

  if (command == "info") {
    return CmdInfo(image_path);
  }

  if (command == "ls") {
    return CmdLs(image_path);
  }

  if (command == "extract") {
    const std::string output_dir = argc >= 4 ? argv[3] : "";
    return CmdExtract(image_path, output_dir);
  }

  if (command == "mount") {
    if (argc != 4 && argc != 6) {
      PrintUsage();
      return 1;
    }

    std::string backend_name = "winfsp";
    if (argc == 6) {
      if (std::string(argv[4]) != "--backend") {
        PrintUsage();
        return 1;
      }
      backend_name = argv[5];
    }

    return CmdMountWithBackend(image_path, argv[3], backend_name);
  }

  PrintUsage();
  return 1;
}
