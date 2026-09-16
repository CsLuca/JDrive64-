#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "jdrive64/disk_image_session.hpp"
#include "jdrive64/winfsp_filesystem.hpp"

namespace {

using jdrive64::DiskImageSession;
using jdrive64::WinFspFilesystem;

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
  return MountStateRoot() / (mount_point + ".state");
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

void PrintUsage() {
  std::cout << "JDrive64 CLI\n"
            << "Usage:\n"
            << "  jdrive64 info <image.d64>\n"
            << "  jdrive64 ls <image.d64>\n"
            << "  jdrive64 extract <image.d64> [output_dir]\n"
            << "  jdrive64 mount <image.d64> <drive_letter:>\n"
            << "  jdrive64 unmount <drive_letter:>\n"
            << "  jdrive64 dir-mounted <drive_letter:>\n"
            << "  jdrive64 read-mounted <drive_letter:> <name.ext>\n";
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

  std::cout << "Disk Name : " << session.Bam().DiskName() << "\n";
  std::cout << "Disk ID   : " << session.Bam().DiskId() << "\n";
  std::cout << "DOS Type  : " << session.Bam().DosType() << "\n";
  std::cout << "Blocks    : " << session.Bam().FreeBlocks() << " Free\n";
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

  WinFspFilesystem fs;
  if (!fs.MountReadOnly(resolved_image.string(), mount_point)) {
    std::cerr << "Error: " << fs.LastError() << "\n";
    return 1;
  }

  MountState state;
  state.mount_point = mount_point;
  state.image_path = resolved_image.string();
  state.files = fs.ReadDirectory();

  std::string save_error;
  if (!SaveMountState(state_file, state, &save_error)) {
    std::cerr << "Error: cannot persist mount state: " << save_error << "\n";
    return 1;
  }

  std::cout << "Mounted " << resolved_image.string() << " on " << mount_point << " (read-only)\n";
  std::cout << fs.GetVolumeInfoText() << "\n";
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

  WinFspFilesystem fs;
  if (!fs.MountReadOnly(state.image_path, mount_point)) {
    std::cerr << "Error: " << fs.LastError() << "\n";
    return 1;
  }

  for (const auto& name : fs.ReadDirectory()) {
    std::cout << name << "\n";
  }

  return 0;
}

int CmdReadMounted(std::string mount_point, const std::string& windows_name) {
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

  WinFspFilesystem fs;
  if (!fs.MountReadOnly(state.image_path, mount_point)) {
    std::cerr << "Error: " << fs.LastError() << "\n";
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

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 1;
  }

  const std::string command = argv[1];

  if (command == "unmount") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    return CmdUnmount(argv[2]);
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
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    return CmdMount(image_path, argv[3]);
  }

  PrintUsage();
  return 1;
}
