#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "jdrive64/bam_reader.hpp"
#include "jdrive64/d64_reader.hpp"
#include "jdrive64/directory_reader.hpp"
#include "jdrive64/file_chain_reader.hpp"

namespace {

using jdrive64::BAMReader;
using jdrive64::D64Reader;
using jdrive64::DirectoryEntry;
using jdrive64::DirectoryReader;
using jdrive64::FileChainReader;

void PrintUsage() {
  std::cout << "JDrive64 CLI\n"
            << "Usage:\n"
            << "  jdrive64 info <image.d64>\n"
            << "  jdrive64 ls <image.d64>\n"
            << "  jdrive64 extract <image.d64> [output_dir]\n";
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

  DirectoryReader dir_reader;
  if (!dir_reader.Load(reader)) {
    std::cerr << "Error: " << dir_reader.LastError() << "\n";
    return 1;
  }

  for (const DirectoryEntry& entry : dir_reader.Entries()) {
    std::cout << entry.name << "." << entry.extension << "  " << entry.size_blocks << " blocks\n";
  }

  return 0;
}

int CmdExtract(const std::string& image_path, const std::string& output_dir_arg) {
  D64Reader reader;
  if (!OpenReader(image_path, reader)) {
    return 1;
  }

  DirectoryReader dir_reader;
  if (!dir_reader.Load(reader)) {
    std::cerr << "Error: " << dir_reader.LastError() << "\n";
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
  for (const DirectoryEntry& entry : dir_reader.Entries()) {
    auto file_data = chain_reader.ReadFile(entry);
    if (!chain_reader.LastError().empty()) {
      std::cerr << "Warning: cannot read " << entry.name << ": " << chain_reader.LastError() << "\n";
      continue;
    }

    const auto file_name = SanitizeFilename(entry.name, entry.extension);
    const auto file_path = output_dir / file_name;
    std::ofstream out(file_path, std::ios::binary);
    if (!out) {
      std::cerr << "Warning: cannot write " << file_path.string() << "\n";
      continue;
    }

    if (!file_data.empty()) {
      out.write(reinterpret_cast<const char*>(file_data.data()),
                static_cast<std::streamsize>(file_data.size()));
    }

    if (!out) {
      std::cerr << "Warning: write failed for " << file_path.string() << "\n";
      continue;
    }

    std::cout << file_name << "\n";
  }

  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    PrintUsage();
    return 1;
  }

  const std::string command = argv[1];
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

  PrintUsage();
  return 1;
}
