#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "jdrive64/bam_reader.hpp"
#include "jdrive64/d64_reader.hpp"
#include "jdrive64/disk_catalog.hpp"
#include "jdrive64/file_chain_reader.hpp"
#include "jdrive64/winfsp_filesystem.hpp"

namespace {

using jdrive64::BAMReader;
using jdrive64::D64Reader;
using jdrive64::DiskCatalog;
using jdrive64::FileChainReader;
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

std::vector<std::string> ReadMountStateLines(const std::filesystem::path& state_file) {
  std::ifstream in(state_file, std::ios::binary);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) {
    lines.push_back(line);
  }
  return lines;
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

bool OpenReader(const std::string& image_path, D64Reader& reader) {
  if (!reader.Open(image_path)) {
    std::cerr << "Error: " << reader.LastError() << "\n";
    return false;
  }
  return true;
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
  D64Reader reader;
  if (!OpenReader(image_path, reader)) {
    return 1;
  }

  BAMReader bam;
  if (!bam.Load(reader)) {
    std::cerr << "Error: " << bam.LastError() << "\n";
    return 1;
  }

  std::cout << "Disk Name : " << bam.DiskName() << "\n";
  std::cout << "Disk ID   : " << bam.DiskId() << "\n";
  std::cout << "DOS Type  : " << bam.DosType() << "\n";
  std::cout << "Blocks    : " << bam.FreeBlocks() << " Free\n";
  return 0;
}

int CmdLs(const std::string& image_path) {
  D64Reader reader;
  if (!OpenReader(image_path, reader)) {
    return 1;
  }

  DiskCatalog catalog;
  if (!catalog.Build(reader)) {
    std::cerr << "Error: " << catalog.LastError() << "\n";
    return 1;
  }

  for (const auto& file : catalog.Files()) {
    std::cout << file.windows_name << "  " << file.size_blocks << " blocks\n";
  }

  return 0;
}

int CmdExtract(const std::string& image_path, const std::string& output_dir_arg) {
  D64Reader reader;
  if (!OpenReader(image_path, reader)) {
    return 1;
  }

  DiskCatalog catalog;
  if (!catalog.Build(reader)) {
    std::cerr << "Error: " << catalog.LastError() << "\n";
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

  FileChainReader chain_reader(reader);
  bool had_warnings = false;
  for (const auto& file : catalog.Files()) {
    auto file_data = chain_reader.ReadFile(file);
    if (!chain_reader.LastError().empty()) {
      std::cerr << "Warning: cannot read " << file.display_name << ": " << chain_reader.LastError()
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
    std::cerr << "Error: mount point already marked as mounted: " << mount_point << "\n";
    return 1;
  }

  WinFspFilesystem fs;
  if (!fs.MountReadOnly(image_path, mount_point)) {
    std::cerr << "Error: " << fs.LastError() << "\n";
    return 1;
  }

  std::ofstream out(state_file, std::ios::binary);
  if (!out) {
    std::cerr << "Error: cannot persist mount state\n";
    return 1;
  }
  out << image_path << "\n";
  for (const auto& name : fs.ReadDirectory()) {
    out << name << "\n";
  }

  std::cout << "Mounted " << image_path << " on " << mount_point << " (read-only)\n";
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

  const auto lines = ReadMountStateLines(state_file);
  if (lines.empty()) {
    std::cerr << "Error: invalid mount state\n";
    return 1;
  }

  for (std::size_t i = 1; i < lines.size(); ++i) {
    if (!lines[i].empty()) {
      std::cout << lines[i] << "\n";
    }
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

  const auto lines = ReadMountStateLines(state_file);
  if (lines.empty() || lines[0].empty()) {
    std::cerr << "Error: invalid mount state\n";
    return 1;
  }

  WinFspFilesystem fs;
  if (!fs.MountReadOnly(lines[0], mount_point)) {
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
