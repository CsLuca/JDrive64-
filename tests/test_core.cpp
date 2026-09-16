#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "jdrive64/bam_reader.hpp"
#include "jdrive64/d64_reader.hpp"
#include "jdrive64/disk_catalog.hpp"
#include "jdrive64/directory_reader.hpp"
#include "jdrive64/file_cache.hpp"
#include "jdrive64/file_chain_reader.hpp"
#include "jdrive64/sector_cache.hpp"
#include "jdrive64/winfsp_filesystem.hpp"

namespace {

using jdrive64::BAMReader;
using jdrive64::D64Reader;
using jdrive64::DiskCatalog;
using jdrive64::DirectoryReader;
using jdrive64::FileCache;
using jdrive64::FileChainReader;
using jdrive64::SectorCache;
using jdrive64::WinFspFilesystem;

bool Assert(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    return false;
  }
  return true;
}

std::uint8_t SectorsPerTrack(std::uint8_t track) {
  if (track >= 1 && track <= 17) {
    return 21;
  }
  if (track <= 24) {
    return 19;
  }
  if (track <= 30) {
    return 18;
  }
  if (track <= 35) {
    return 17;
  }
  return 0;
}

std::size_t TrackSectorToOffset(std::uint8_t track, std::uint8_t sector) {
  std::size_t sectors_before = 0;
  for (std::uint8_t t = 1; t < track; ++t) {
    sectors_before += SectorsPerTrack(t);
  }
  return (sectors_before + sector) * D64Reader::kSectorSize;
}

void WriteSector(std::vector<std::uint8_t>& image,
                 std::uint8_t track,
                 std::uint8_t sector,
                 const std::array<std::uint8_t, D64Reader::kSectorSize>& data) {
  const auto offset = TrackSectorToOffset(track, sector);
  std::copy(data.begin(), data.end(), image.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::filesystem::path CreateSampleD64() {
  std::vector<std::uint8_t> image(174848, 0);

  std::array<std::uint8_t, D64Reader::kSectorSize> bam{};
  bam[4 + (1 - 1) * 4] = 10;
  bam[4 + (2 - 1) * 4] = 20;

  const std::string disk_name = "TEST DISK";
  for (std::size_t i = 0; i < 16; ++i) {
    bam[0x90 + i] = (i < disk_name.size()) ? static_cast<std::uint8_t>(disk_name[i]) : 0xA0;
  }
  bam[0xA2] = 'A';
  bam[0xA3] = 'B';
  bam[0xA5] = '2';
  bam[0xA6] = 'A';
  WriteSector(image, 18, 0, bam);

  std::array<std::uint8_t, D64Reader::kSectorSize> dir{};
  dir[0] = 0;
  dir[1] = 0;
  dir[2 + 2] = 0x82;
  dir[2 + 3] = 1;
  dir[2 + 4] = 0;
  const std::string file_name = "HELLO";
  for (std::size_t i = 0; i < 16; ++i) {
    dir[2 + 5 + i] = (i < file_name.size()) ? static_cast<std::uint8_t>(file_name[i]) : 0xA0;
  }
  dir[2 + 30] = 1;
  dir[2 + 31] = 0;
  WriteSector(image, 18, 1, dir);

  std::array<std::uint8_t, D64Reader::kSectorSize> file_sector{};
  file_sector[0] = 0;
  file_sector[1] = 7;
  file_sector[2] = 'H';
  file_sector[3] = 'E';
  file_sector[4] = 'L';
  file_sector[5] = 'L';
  file_sector[6] = 'O';
  WriteSector(image, 1, 0, file_sector);

  const auto path = std::filesystem::temp_directory_path() / "jdrive64_test_image.d64";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
  return path;
}

bool TestCoreParsers(const std::filesystem::path& image_path) {
  D64Reader reader;
  if (!Assert(reader.Open(image_path.string()), "D64Reader opens sample image")) {
    return false;
  }

  if (!Assert(reader.SectorsPerTrack(1) == 21, "track 1 has 21 sectors")) {
    return false;
  }
  if (!Assert(reader.SectorsPerTrack(18) == 19, "track 18 has 19 sectors")) {
    return false;
  }
  if (!Assert(reader.TrackSectorToOffset(1, 0) == 0, "offset T1/S0 is zero")) {
    return false;
  }

  BAMReader bam;
  if (!Assert(bam.Load(reader), "BAM loads")) {
    return false;
  }
  if (!Assert(bam.FreeBlocks() == 30, "BAM free blocks parsed")) {
    return false;
  }
  if (!Assert(bam.DiskName() == "TEST DISK", "BAM disk name parsed")) {
    return false;
  }

  DirectoryReader directory;
  if (!Assert(directory.Load(reader), "Directory loads")) {
    return false;
  }
  if (!Assert(directory.Entries().size() == 1, "Directory has one file")) {
    return false;
  }
  if (!Assert(directory.Entries()[0].name == "HELLO", "Directory file name parsed")) {
    return false;
  }
  if (!Assert(directory.Entries()[0].extension == "PRG", "Directory extension parsed")) {
    return false;
  }

  FileChainReader chain(reader);
  const auto bytes = chain.ReadFile(directory.Entries()[0]);
  if (!Assert(chain.LastError().empty(), "File chain has no error")) {
    return false;
  }
  const std::string text(bytes.begin(), bytes.end());
  if (!Assert(text == "HELLO", "File chain reconstructs payload")) {
    return false;
  }

  DiskCatalog catalog;
  if (!Assert(catalog.Build(reader), "Disk catalog builds")) {
    return false;
  }
  if (!Assert(catalog.FindByWindowsName("HELLO.PRG") != nullptr, "Catalog lookup works")) {
    return false;
  }

  return true;
}

bool TestCaches() {
  SectorCache sector_cache(1);
  std::array<std::uint8_t, D64Reader::kSectorSize> a{};
  std::array<std::uint8_t, D64Reader::kSectorSize> b{};
  a[0] = 11;
  b[0] = 22;

  std::vector<std::uint8_t> out;
  if (!Assert(!sector_cache.Get(1, 0, &out), "Sector cache first miss")) {
    return false;
  }
  sector_cache.Put(1, 0, a.data(), a.size());
  if (!Assert(sector_cache.Get(1, 0, &out), "Sector cache hit after put")) {
    return false;
  }
  if (!Assert(!out.empty() && out[0] == 11, "Sector cache returns expected bytes")) {
    return false;
  }
  sector_cache.Put(1, 1, b.data(), b.size());
  if (!Assert(!sector_cache.Get(1, 0, &out), "Sector cache evicts oldest entry")) {
    return false;
  }

  FileCache file_cache(1);
  if (!Assert(!file_cache.Get("A", &out), "File cache first miss")) {
    return false;
  }
  file_cache.Put("A", std::vector<std::uint8_t>{1, 2, 3});
  if (!Assert(file_cache.Get("A", &out), "File cache hit after put")) {
    return false;
  }
  file_cache.Put("B", std::vector<std::uint8_t>{4});
  if (!Assert(!file_cache.Get("A", &out), "File cache evicts oldest entry")) {
    return false;
  }

  return true;
}

bool TestWinFspFacade(const std::filesystem::path& image_path) {
  WinFspFilesystem fs;
  if (!Assert(fs.MountReadOnly(image_path.string(), "Z:"), "Mount facade succeeds")) {
    return false;
  }
  if (!Assert(fs.IsMounted(), "Mount facade marks state")) {
    return false;
  }

  const auto names = fs.ReadDirectory();
  if (!Assert(names.size() == 1 && names[0] == "HELLO.PRG", "ReadDirectory lists HELLO.PRG")) {
    return false;
  }

  std::vector<std::uint8_t> data;
  if (!Assert(fs.ReadFileByWindowsName("HELLO.PRG", &data), "ReadFileByWindowsName works")) {
    return false;
  }
  if (!Assert(std::string(data.begin(), data.end()) == "HELLO", "Read file bytes are correct")) {
    return false;
  }

  if (!Assert(fs.Unmount("z:"), "Unmount accepts case-insensitive mount point")) {
    return false;
  }

  if (!Assert(fs.MountReadOnly(image_path.string(), "Z:"), "Remount for callback-like API tests")) {
    return false;
  }

  WinFspFilesystem::VolumeInfo volume_info;
  if (!Assert(fs.GetVolumeInfo(&volume_info), "GetVolumeInfo works")) {
    return false;
  }
  if (!Assert(volume_info.filesystem == "JDrive64", "GetVolumeInfo filesystem")) {
    return false;
  }

  WinFspFilesystem::FileInfo file_info;
  if (!Assert(fs.GetFileInfo("HELLO.PRG", &file_info), "GetFileInfo for HELLO.PRG")) {
    return false;
  }
  if (!Assert(file_info.size_bytes == 5, "GetFileInfo size for HELLO.PRG")) {
    return false;
  }

  std::uint64_t handle = 0;
  if (!Assert(fs.Open("HELLO.PRG", &handle), "Open works")) {
    return false;
  }

  std::vector<std::uint8_t> slice;
  if (!Assert(fs.Read(handle, 1, 3, &slice), "Read works")) {
    return false;
  }
  if (!Assert(std::string(slice.begin(), slice.end()) == "ELL", "Read returns expected range")) {
    return false;
  }

  const auto stats = fs.GetRuntimeStats();
  if (!Assert(stats.read_ops >= 2, "RuntimeStats read_ops increments")) {
    return false;
  }
  if (!Assert(stats.bytes_served >= 8, "RuntimeStats bytes_served increments")) {
    return false;
  }
  if (!Assert(stats.file_cache.hits + stats.file_cache.misses > 0,
              "RuntimeStats file cache telemetry available")) {
    return false;
  }

  const auto stats_text = fs.GetRuntimeStatsText();
  if (!Assert(stats_text.find("SectorCache") != std::string::npos,
              "RuntimeStatsText contains SectorCache")) {
    return false;
  }
  if (!Assert(stats_text.find("FileCache") != std::string::npos,
              "RuntimeStatsText contains FileCache")) {
    return false;
  }

  if (!Assert(fs.Close(handle), "Close works")) {
    return false;
  }
  if (!Assert(!fs.Read(handle, 0, 1, &slice), "Read with closed handle fails")) {
    return false;
  }
  if (!Assert(fs.LastError() == "Invalid handle", "Read closed handle error")) {
    return false;
  }

  if (!Assert(!fs.WriteFileByWindowsName("HELLO.PRG", std::vector<std::uint8_t>{1}),
              "Write is denied in read-only mode")) {
    return false;
  }
  if (!Assert(fs.LastError() == "ACCESS_DENIED", "Write returns ACCESS_DENIED")) {
    return false;
  }

  if (!Assert(!fs.DeleteByWindowsName("HELLO.PRG"), "Delete is denied in read-only mode")) {
    return false;
  }
  if (!Assert(fs.LastError() == "ACCESS_DENIED", "Delete returns ACCESS_DENIED")) {
    return false;
  }

  if (!Assert(!fs.RenameByWindowsName("HELLO.PRG", "NEW.PRG"),
              "Rename is denied in read-only mode")) {
    return false;
  }
  if (!Assert(fs.LastError() == "ACCESS_DENIED", "Rename returns ACCESS_DENIED")) {
    return false;
  }

  if (!Assert(!fs.CreateByWindowsName("NEWFILE.PRG"), "Create is denied in read-only mode")) {
    return false;
  }
  if (!Assert(fs.LastError() == "ACCESS_DENIED", "Create returns ACCESS_DENIED")) {
    return false;
  }

  if (!Assert(!fs.SetFileSizeByWindowsName("HELLO.PRG", 123),
              "SetFileSize is denied in read-only mode")) {
    return false;
  }
  if (!Assert(fs.LastError() == "ACCESS_DENIED", "SetFileSize returns ACCESS_DENIED")) {
    return false;
  }

  if (!Assert(!fs.SetFileAttributesByWindowsName("HELLO.PRG", 0x20),
              "SetFileAttributes is denied in read-only mode")) {
    return false;
  }
  if (!Assert(fs.LastError() == "ACCESS_DENIED", "SetFileAttributes returns ACCESS_DENIED")) {
    return false;
  }

  if (!Assert(fs.Unmount("Z:"), "Final unmount succeeds")) {
    return false;
  }

  return true;
}

}  // namespace

int main() {
  const auto image_path = CreateSampleD64();

  bool ok = true;
  ok = ok && TestCoreParsers(image_path);
  ok = ok && TestCaches();
  ok = ok && TestWinFspFacade(image_path);

  std::error_code ec;
  std::filesystem::remove(image_path, ec);

  if (!ok) {
    return 1;
  }

  std::cout << "All tests passed\n";
  return 0;
}
