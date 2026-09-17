#include <array>
#include <cstring>
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
#include "jdrive64/kernel_mount_manager.hpp"
#include "jdrive64/kernel_ipc_channel.hpp"
#include "jdrive64/kernel_ioctl_protocol.hpp"
#include "jdrive64/kernel_readonly_fs.hpp"
#include "jdrive64/kernel_transport.hpp"
#include "jdrive64/kernel_user_bridge.hpp"
#include "jdrive64/mount_backend.hpp"
#include "jdrive64/sector_cache.hpp"
#include "jdrive64/winfsp_adapter.hpp"
#include "jdrive64/winfsp_callbacks.hpp"
#include "jdrive64/winfsp_filesystem.hpp"
#include "jdrive64/winfsp_native_bridge.hpp"
#include "jdrive64/winfsp_runtime.hpp"

namespace {

using jdrive64::BAMReader;
using jdrive64::D64Reader;
using jdrive64::DiskCatalog;
using jdrive64::DirectoryReader;
using jdrive64::FileCache;
using jdrive64::FileChainReader;
using jdrive64::IMountBackend;
using jdrive64::KernelMountManager;
using jdrive64::KernelIpcChannel;
using jdrive64::KernelOpcode;
using jdrive64::KernelRequest;
using jdrive64::KernelResponse;
using jdrive64::KernelReadOnlyFilesystem;
using jdrive64::KernelTransport;
using jdrive64::KernelUserBridge;
using jdrive64::SectorCache;
using jdrive64::WinFspAdapter;
using jdrive64::WinFspCallbacks;
using jdrive64::WinFspFilesystem;
using jdrive64::WinFspNativeApi;
using jdrive64::WinFspNativeBridge;
using jdrive64::WinFspRuntime;

class FakeDeviceIoApi final : public KernelTransport::DeviceIoApi {
 public:
  bool open_ok = true;
  bool close_ok = true;
  bool ioctl_ok = true;

  std::string open_error = "Kernel device channel not available";
  std::string close_error = "Failed to close device handle";
  std::string ioctl_error = "DeviceIoControl failed";

  std::vector<std::uint8_t> next_response_frame;
  std::vector<std::uint8_t> last_request_frame;
  void* fake_handle = reinterpret_cast<void*>(0x1234);
  int open_calls = 0;
  int close_calls = 0;
  int ioctl_calls = 0;

  bool Open(const std::string& device_path, void** handle, std::string* error) override {
    ++open_calls;
    if (device_path != "\\\\.\\JDrive64Kdrv" || handle == nullptr || error == nullptr) {
      return false;
    }
    if (!open_ok) {
      *error = open_error;
      return false;
    }
    *handle = fake_handle;
    error->clear();
    return true;
  }

  bool Close(void* handle, std::string* error) override {
    ++close_calls;
    if (error == nullptr || handle != fake_handle) {
      return false;
    }
    if (!close_ok) {
      *error = close_error;
      return false;
    }
    error->clear();
    return true;
  }

  bool Ioctl(void* handle,
             const std::vector<std::uint8_t>& request_frame,
             std::vector<std::uint8_t>* response_frame,
             std::string* error) override {
    ++ioctl_calls;
    if (error == nullptr || response_frame == nullptr || handle != fake_handle) {
      return false;
    }
    last_request_frame = request_frame;
    if (!ioctl_ok) {
      *error = ioctl_error;
      return false;
    }
    *response_frame = next_response_frame;
    error->clear();
    return true;
  }
};

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

  if (!Assert(file_cache.Capacity() == 1, "File cache capacity reports value")) {
    return false;
  }
  if (!Assert(file_cache.Size() <= file_cache.Capacity(), "File cache size within capacity")) {
    return false;
  }
  if (!Assert(file_cache.HitRate() >= 0.0 && file_cache.HitRate() <= 1.0,
              "File cache hit rate in range")) {
    return false;
  }
  file_cache.ResetStats();
  if (!Assert(file_cache.Hits() == 0 && file_cache.Misses() == 0, "File cache reset stats")) {
    return false;
  }

  sector_cache.SetCapacity(2);
  if (!Assert(sector_cache.Capacity() == 2, "Sector cache capacity can be updated")) {
    return false;
  }
  sector_cache.ResetStats();
  if (!Assert(sector_cache.Hits() == 0 && sector_cache.Misses() == 0, "Sector cache reset stats")) {
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

  fs.ConfigureCaches(16, 16, 2);

  WinFspFilesystem::VolumeInfo volume_info;
  if (!Assert(fs.GetVolumeInfo(&volume_info), "GetVolumeInfo works")) {
    return false;
  }
  if (!Assert(volume_info.filesystem == "JDrive64", "GetVolumeInfo filesystem")) {
    return false;
  }
  if (!Assert(volume_info.block_size_bytes == 256, "GetVolumeInfo block size")) {
    return false;
  }
  if (!Assert(volume_info.capacity_blocks == 664, "GetVolumeInfo capacity blocks")) {
    return false;
  }
  if (!Assert(volume_info.free_blocks == 30, "GetVolumeInfo free blocks")) {
    return false;
  }
  if (!Assert(volume_info.used_blocks == 634, "GetVolumeInfo used blocks")) {
    return false;
  }
  if (!Assert(volume_info.capacity_bytes == 169984, "GetVolumeInfo capacity bytes")) {
    return false;
  }
  if (!Assert(volume_info.free_bytes == 7680, "GetVolumeInfo free bytes")) {
    return false;
  }
  if (!Assert(volume_info.used_bytes == 162304, "GetVolumeInfo used bytes")) {
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
  if (!Assert(stats.avg_read_latency_us >= 0.0, "RuntimeStats avg latency available")) {
    return false;
  }
  if (!Assert(stats.throughput_bytes_per_sec >= 0.0, "RuntimeStats throughput available")) {
    return false;
  }
  if (!Assert(stats.file_cache_max_item_size == 2, "RuntimeStats reports file cache max item size")) {
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
  if (!Assert(fs.LastStatus() == WinFspFilesystem::FsStatus::kInvalidHandle,
              "Read closed handle status is kInvalidHandle")) {
    return false;
  }
  if (!Assert(fs.LastWin32Error() == 6, "Read closed handle Win32 error is ERROR_INVALID_HANDLE")) {
    return false;
  }

  if (!Assert(!fs.WriteFileByWindowsName("HELLO.PRG", std::vector<std::uint8_t>{1}),
              "Write is denied in read-only mode")) {
    return false;
  }
  if (!Assert(fs.LastError() == "ACCESS_DENIED", "Write returns ACCESS_DENIED")) {
    return false;
  }
  if (!Assert(fs.LastStatus() == WinFspFilesystem::FsStatus::kAccessDenied,
              "Write status is kAccessDenied")) {
    return false;
  }
  if (!Assert(fs.LastWin32Error() == 5, "Write Win32 error is ERROR_ACCESS_DENIED")) {
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

  WinFspFilesystem fs2;
  std::vector<std::uint8_t> tmp;
  if (!Assert(!fs2.ReadFileByWindowsName("HELLO.PRG", &tmp), "Read before mount fails")) {
    return false;
  }
  if (!Assert(fs2.LastStatus() == WinFspFilesystem::FsStatus::kNotMounted,
              "Read before mount status is kNotMounted")) {
    return false;
  }
  if (!Assert(fs2.LastWin32Error() == 21, "Read before mount Win32 error is ERROR_NOT_READY")) {
    return false;
  }

  if (!Assert(!fs.GetFileInfo("MISSING.PRG", &file_info), "GetFileInfo missing fails")) {
    return false;
  }
  if (!Assert(fs.LastStatus() == WinFspFilesystem::FsStatus::kFileNotFound,
              "GetFileInfo missing status is kFileNotFound")) {
    return false;
  }
  if (!Assert(fs.LastWin32Error() == 2, "GetFileInfo missing Win32 error is ERROR_FILE_NOT_FOUND")) {
    return false;
  }

  if (!Assert(fs.Unmount("Z:"), "Final unmount succeeds")) {
    return false;
  }

  return true;
}

bool TestWinFspRuntimeScaffold(const std::filesystem::path& image_path) {
  WinFspRuntime runtime;
#if defined(JDRIVE64_ENABLE_WINFSP)
  if (!Assert(runtime.StartReadOnly(image_path.string(), "Y:"),
              "WinFspRuntime start succeeds when WinFsp support is enabled")) {
    return false;
  }
  if (!Assert(runtime.IsRunning(), "WinFspRuntime running state after start")) {
    return false;
  }
  if (!Assert(runtime.Stop(), "WinFspRuntime stop succeeds")) {
    return false;
  }
  if (!Assert(!runtime.IsRunning(), "WinFspRuntime not running after stop")) {
    return false;
  }
#else
  if (!Assert(!runtime.StartReadOnly(image_path.string(), "Y:"),
              "WinFspRuntime start fails when WinFsp support is disabled")) {
    return false;
  }
  if (!Assert(runtime.LastError().find("disabled") != std::string::npos,
              "WinFspRuntime returns disabled error")) {
    return false;
  }
  if (!Assert(!runtime.Stop(), "WinFspRuntime stop fails when not running")) {
    return false;
  }
  if (!Assert(runtime.LastError() == "Runtime is not running",
              "WinFspRuntime stop reports not running")) {
    return false;
  }
#endif
  return true;
}

bool TestWinFspAdapterScaffold(const std::filesystem::path& image_path) {
  WinFspAdapter adapter;
  WinFspFilesystem filesystem;
#if defined(JDRIVE64_ENABLE_WINFSP)
  if (!Assert(adapter.StartReadOnly(&filesystem, image_path.string(), "X:"),
              "WinFspAdapter start succeeds when WinFsp support is enabled")) {
    return false;
  }
  if (!Assert(adapter.IsCallbacksInitialized(),
              "WinFspAdapter callback table initialized when WinFsp support is enabled")) {
    return false;
  }
  if (!Assert(adapter.IsNativeRegistered(),
              "WinFspAdapter native bridge registered when WinFsp support is enabled")) {
    return false;
  }
  if (!Assert(adapter.Stop(&filesystem, "X:"),
              "WinFspAdapter stop succeeds when WinFsp support is enabled")) {
    return false;
  }
#else
  if (!Assert(!adapter.StartReadOnly(&filesystem, image_path.string(), "X:"),
              "WinFspAdapter start fails when WinFsp support is disabled")) {
    return false;
  }
  if (!Assert(adapter.LastError().find("disabled") != std::string::npos,
              "WinFspAdapter reports disabled error")) {
    return false;
  }
  if (!Assert(!adapter.Stop(&filesystem, "X:"),
              "WinFspAdapter stop fails when WinFsp support is disabled")) {
    return false;
  }
#endif

  return true;
}

bool TestWinFspCallbacksBridge(const std::filesystem::path& image_path) {
  WinFspFilesystem fs;
  if (!Assert(fs.MountReadOnly(image_path.string(), "W:"), "Callbacks bridge mount facade")) {
    return false;
  }

  WinFspCallbacks callbacks;
  if (!Assert(callbacks.Initialize(&fs), "Callbacks initialize")) {
    return false;
  }
  if (!Assert(callbacks.IsInitialized(), "Callbacks initialized state")) {
    return false;
  }

  WinFspFilesystem::VolumeInfo volume;
  if (!Assert(callbacks.DispatchGetVolumeInfo(&volume), "Callbacks dispatch volume info")) {
    return false;
  }
  if (!Assert(volume.filesystem == "JDrive64", "Callbacks volume filesystem")) {
    return false;
  }

  std::vector<std::string> entries;
  if (!Assert(callbacks.DispatchReadDirectory(&entries), "Callbacks dispatch read directory")) {
    return false;
  }
  if (!Assert(entries.size() == 1 && entries[0] == "HELLO.PRG", "Callbacks directory entries")) {
    return false;
  }

  std::uint64_t handle = 0;
  if (!Assert(callbacks.DispatchOpen("HELLO.PRG", &handle), "Callbacks dispatch open")) {
    return false;
  }

  std::vector<std::uint8_t> bytes;
  if (!Assert(callbacks.DispatchRead(handle, 0, 5, &bytes), "Callbacks dispatch read")) {
    return false;
  }
  if (!Assert(std::string(bytes.begin(), bytes.end()) == "HELLO", "Callbacks read payload")) {
    return false;
  }

  if (!Assert(callbacks.DispatchClose(handle), "Callbacks dispatch close")) {
    return false;
  }

  callbacks.Shutdown();
  if (!Assert(!callbacks.IsInitialized(), "Callbacks shutdown state")) {
    return false;
  }
  if (!Assert(!callbacks.DispatchReadDirectory(&entries), "Callbacks dispatch fails after shutdown")) {
    return false;
  }

  if (!Assert(fs.Unmount("W:"), "Callbacks bridge unmount facade")) {
    return false;
  }

  return true;
}

bool TestWinFspNativeBridgeScaffold(const std::filesystem::path& image_path) {
  WinFspFilesystem fs;
  if (!Assert(fs.MountReadOnly(image_path.string(), "V:"), "Native bridge mount facade")) {
    return false;
  }

  WinFspCallbacks callbacks;
  if (!Assert(callbacks.Initialize(&fs), "Native bridge callbacks initialize")) {
    return false;
  }

  WinFspNativeBridge bridge;
#if defined(JDRIVE64_ENABLE_WINFSP)
  if (!Assert(bridge.RegisterReadOnly("V:", callbacks),
              "Native bridge register succeeds when WinFsp support is enabled")) {
    return false;
  }
  if (!Assert(bridge.IsRegistered(), "Native bridge registered state")) {
    return false;
  }
  if (!Assert(bridge.RegisteredMountPoint() == "V:", "Native bridge mount point tracked")) {
    return false;
  }
  if (!Assert(bridge.Unregister(), "Native bridge unregister succeeds")) {
    return false;
  }
  if (!Assert(!bridge.IsRegistered(), "Native bridge unregistered state")) {
    return false;
  }
#else
  if (!Assert(!bridge.RegisterReadOnly("V:", callbacks),
              "Native bridge register fails when WinFsp support is disabled")) {
    return false;
  }
  if (!Assert(bridge.LastError().find("disabled") != std::string::npos,
              "Native bridge reports disabled error")) {
    return false;
  }
#endif

  callbacks.Shutdown();
  if (!Assert(fs.Unmount("V:"), "Native bridge unmount facade")) {
    return false;
  }

  return true;
}

class FakeNativeApi final : public WinFspNativeApi {
 public:
  bool register_result = true;
  bool unregister_result = true;
  std::string register_error;
  std::string unregister_error;
  int register_calls = 0;
  int unregister_calls = 0;

  bool RegisterReadOnly(const std::string& mount_point,
                        const WinFspCallbacks& callbacks,
                        std::string* error_out) override {
    (void)mount_point;
    (void)callbacks;
    ++register_calls;
    if (error_out != nullptr) {
      *error_out = register_error;
    }
    return register_result;
  }

  bool Unregister(std::string* error_out) override {
    ++unregister_calls;
    if (error_out != nullptr) {
      *error_out = unregister_error;
    }
    return unregister_result;
  }
};

bool TestWinFspNativeBridgeProviderInjection(const std::filesystem::path& image_path) {
  WinFspFilesystem fs;
  if (!Assert(fs.MountReadOnly(image_path.string(), "U:"), "Provider injection mount facade")) {
    return false;
  }

  WinFspCallbacks callbacks;
  if (!Assert(callbacks.Initialize(&fs), "Provider injection callbacks initialize")) {
    return false;
  }

  FakeNativeApi fake_api;
  WinFspNativeBridge bridge;
  bridge.SetApi(&fake_api);

  fake_api.register_result = false;
  fake_api.register_error = "fake register failure";
  if (!Assert(!bridge.RegisterReadOnly("U:", callbacks), "Provider injection register failure path")) {
    return false;
  }
  if (!Assert(bridge.LastError() == "fake register failure", "Provider injection register error propagation")) {
    return false;
  }
  if (!Assert(fake_api.register_calls == 1, "Provider injection register call count")) {
    return false;
  }

  fake_api.register_result = true;
  fake_api.register_error.clear();
  if (!Assert(bridge.RegisterReadOnly("U:", callbacks), "Provider injection register success path")) {
    return false;
  }
  if (!Assert(bridge.IsRegistered(), "Provider injection registered state")) {
    return false;
  }

  fake_api.unregister_result = false;
  fake_api.unregister_error = "fake unregister failure";
  if (!Assert(!bridge.Unregister(), "Provider injection unregister failure path")) {
    return false;
  }
  if (!Assert(bridge.LastError() == "fake unregister failure",
              "Provider injection unregister error propagation")) {
    return false;
  }
  if (!Assert(fake_api.unregister_calls == 1, "Provider injection unregister call count")) {
    return false;
  }

  fake_api.unregister_result = true;
  fake_api.unregister_error.clear();
  if (!Assert(bridge.Unregister(), "Provider injection unregister success path")) {
    return false;
  }
  if (!Assert(!bridge.IsRegistered(), "Provider injection unregistered state")) {
    return false;
  }

  callbacks.Shutdown();
  if (!Assert(fs.Unmount("U:"), "Provider injection unmount facade")) {
    return false;
  }

  return true;
}

bool TestWinFspDefaultNativeApiContract(const std::filesystem::path& image_path) {
  WinFspFilesystem fs;
  if (!Assert(fs.MountReadOnly(image_path.string(), "T:"), "Default native API mount facade")) {
    return false;
  }

  WinFspCallbacks callbacks;
  if (!Assert(callbacks.Initialize(&fs), "Default native API callbacks initialize")) {
    return false;
  }

  jdrive64::DefaultWinFspNativeApi api;
  std::string error;
#if defined(JDRIVE64_ENABLE_WINFSP)
  if (api.RegisterReadOnly("T:", callbacks, &error)) {
    if (!Assert(api.Unregister(&error), "Default native API unregister after successful register")) {
      return false;
    }
  } else {
    if (!Assert(!error.empty(), "Default native API returns explicit error when register fails")) {
      return false;
    }
  }
#else
  if (!Assert(!api.RegisterReadOnly("T:", callbacks, &error),
              "Default native API register fails when WinFsp support is disabled")) {
    return false;
  }
  if (!Assert(error.find("disabled") != std::string::npos,
              "Default native API disabled message")) {
    return false;
  }
#endif

  callbacks.Shutdown();
  if (!Assert(fs.Unmount("T:"), "Default native API unmount facade")) {
    return false;
  }

  return true;
}

bool TestMountBackendFactory(const std::filesystem::path& image_path) {
  std::string normalized;
  std::string error;

  auto winfsp = jdrive64::CreateMountBackend("winfsp", &normalized, &error);
  if (!Assert(winfsp != nullptr, "Mount backend factory creates winfsp backend")) {
    return false;
  }
  if (!Assert(normalized == "winfsp", "Mount backend factory normalizes winfsp name")) {
    return false;
  }
  if (!Assert(winfsp->MountReadOnly(image_path.string(), "Q:"), "Winfsp backend mount through interface")) {
    return false;
  }
  if (!Assert(winfsp->HealthCheck(), "Winfsp backend health check")) {
    return false;
  }
  if (!Assert(winfsp->Unmount("Q:"), "Winfsp backend unmount through interface")) {
    return false;
  }

  auto kdrv = jdrive64::CreateMountBackend("kdrv", &normalized, &error);
  if (!Assert(kdrv != nullptr, "Mount backend factory creates kdrv backend")) {
    return false;
  }
  if (!Assert(normalized == "kdrv", "Mount backend factory normalizes kdrv name")) {
    return false;
  }
  if (!Assert(!kdrv->MountReadOnly(image_path.string(), "Q:"), "Kdrv backend not implemented yet")) {
    return false;
  }
  if (!Assert(kdrv->LastError().find("not implemented") != std::string::npos,
              "Kdrv backend returns scaffold not implemented error")) {
    return false;
  }

  auto unknown = jdrive64::CreateMountBackend("unknown", &normalized, &error);
  if (!Assert(unknown == nullptr, "Mount backend factory rejects unknown backend")) {
    return false;
  }
  if (!Assert(error.find("Unknown backend") != std::string::npos,
              "Mount backend factory returns unknown backend error")) {
    return false;
  }

  return true;
}

bool TestKernelMountManagerScaffold() {
  KernelMountManager manager;

  if (!Assert(!manager.AssignDriveLetter("BAD"), "KernelMountManager rejects invalid mount point")) {
    return false;
  }
  if (!Assert(manager.LastError() == "Invalid mount point", "KernelMountManager invalid mount point error")) {
    return false;
  }

  if (!Assert(!manager.AssignDriveLetter("R:"),
              "KernelMountManager returns deterministic not-implemented on assign")) {
    return false;
  }
  if (!Assert(manager.LastError().find("not implemented") != std::string::npos,
              "KernelMountManager assign not-implemented error")) {
    return false;
  }
  if (!Assert(manager.IsAssigned(), "KernelMountManager tracks assigned state")) {
    return false;
  }
  if (!Assert(manager.AssignedMountPoint() == "R:", "KernelMountManager tracks assigned mount point")) {
    return false;
  }

  if (!Assert(!manager.ReleaseDriveLetter("Q:"),
              "KernelMountManager rejects mismatched release mount point")) {
    return false;
  }
  if (!Assert(manager.LastError() == "Assigned drive letter mismatch",
              "KernelMountManager mismatch release error")) {
    return false;
  }

  if (!Assert(manager.ReleaseDriveLetter("R:"), "KernelMountManager releases assigned mount point")) {
    return false;
  }
  if (!Assert(!manager.IsAssigned(), "KernelMountManager clears assigned state")) {
    return false;
  }

  return true;
}

bool TestKernelReadOnlyFilesystemScaffold(const std::filesystem::path& image_path) {
  KernelReadOnlyFilesystem fs;
  if (!Assert(fs.OpenImage(image_path.string()), "KernelReadOnlyFilesystem open image")) {
    return false;
  }

  std::vector<std::string> entries;
  if (!Assert(fs.ReadDirectory(&entries), "KernelReadOnlyFilesystem read directory")) {
    return false;
  }
  if (!Assert(entries.size() == 1 && entries[0] == "HELLO.PRG",
              "KernelReadOnlyFilesystem directory entries")) {
    return false;
  }

  KernelReadOnlyFilesystem::FileInfo info;
  if (!Assert(fs.QueryFile("HELLO.PRG", &info), "KernelReadOnlyFilesystem query file")) {
    return false;
  }
  if (!Assert(info.size_bytes == 5, "KernelReadOnlyFilesystem query file size")) {
    return false;
  }

  std::uint64_t handle = 0;
  if (!Assert(fs.OpenFile("HELLO.PRG", &handle), "KernelReadOnlyFilesystem open file handle")) {
    return false;
  }

  std::vector<std::uint8_t> bytes;
  if (!Assert(fs.ReadFile(handle, 1, 3, &bytes), "KernelReadOnlyFilesystem read slice")) {
    return false;
  }
  if (!Assert(std::string(bytes.begin(), bytes.end()) == "ELL",
              "KernelReadOnlyFilesystem read slice payload")) {
    return false;
  }

  if (!Assert(fs.CloseFile(handle), "KernelReadOnlyFilesystem close handle")) {
    return false;
  }
  if (!Assert(!fs.ReadFile(handle, 0, 1, &bytes), "KernelReadOnlyFilesystem reject closed handle")) {
    return false;
  }

  return true;
}

bool TestKernelUserBridgeScaffold(const std::filesystem::path& image_path) {
  KernelUserBridge bridge;
  if (!Assert(bridge.Initialize(image_path.string()), "KernelUserBridge initialize")) {
    return false;
  }

  KernelResponse response;
  if (!Assert(bridge.Dispatch(KernelRequest{KernelOpcode::kReadDirectory, "", 0, 0, 0}, &response),
              "KernelUserBridge dispatch read directory")) {
    return false;
  }
  if (!Assert(response.success, "KernelUserBridge read directory success flag")) {
    return false;
  }
  if (!Assert(response.directory_entries.size() == 1 && response.directory_entries[0] == "HELLO.PRG",
              "KernelUserBridge read directory entries")) {
    return false;
  }

  if (!Assert(bridge.Dispatch(KernelRequest{KernelOpcode::kOpenFile, "HELLO.PRG", 0, 0, 0}, &response),
              "KernelUserBridge dispatch open")) {
    return false;
  }
  const std::uint64_t handle = response.handle;
  if (!Assert(handle != 0, "KernelUserBridge open handle non-zero")) {
    return false;
  }

  if (!Assert(bridge.Dispatch(KernelRequest{KernelOpcode::kReadFile, "", handle, 1, 3}, &response),
              "KernelUserBridge dispatch read")) {
    return false;
  }
  if (!Assert(std::string(response.data.begin(), response.data.end()) == "ELL",
              "KernelUserBridge read payload")) {
    return false;
  }

  if (!Assert(bridge.Dispatch(KernelRequest{KernelOpcode::kCloseFile, "", handle, 0, 0}, &response),
              "KernelUserBridge dispatch close")) {
    return false;
  }

  if (!Assert(!bridge.Dispatch(KernelRequest{KernelOpcode::kInvalid, "", 0, 0, 0}, &response),
              "KernelUserBridge invalid opcode fails")) {
    return false;
  }
  if (!Assert(response.error == "Unsupported opcode", "KernelUserBridge invalid opcode error")) {
    return false;
  }

  return true;
}

bool TestKernelIpcChannelScaffold(const std::filesystem::path& image_path) {
  KernelIpcChannel channel;

  if (!Assert(channel.GetTransportMode() == KernelTransport::Mode::kLoopback,
              "KernelIpcChannel default transport mode is loopback")) {
    return false;
  }
  if (!Assert(channel.SetTransportMode(KernelTransport::Mode::kDevice),
              "KernelIpcChannel transport mode setter accepts device")) {
    return false;
  }
  if (!Assert(channel.GetTransportMode() == KernelTransport::Mode::kDevice,
              "KernelIpcChannel transport mode setter works")) {
    return false;
  }
  if (!Assert(channel.SetTransportMode(KernelTransport::Mode::kLoopback),
              "KernelIpcChannel transport mode setter accepts loopback")) {
    return false;
  }

  KernelResponse response;
  if (!Assert(!channel.Send(KernelRequest{KernelOpcode::kReadDirectory, "", 0, 0, 0}, &response),
              "KernelIpcChannel send fails before connect")) {
    return false;
  }

  if (!Assert(channel.Connect(image_path.string()), "KernelIpcChannel connect")) {
    return false;
  }
  if (!Assert(!channel.SetTransportMode(KernelTransport::Mode::kDevice),
              "KernelIpcChannel rejects mode switch while connected")) {
    return false;
  }
  if (!Assert(channel.LastError().find("while connected") != std::string::npos,
              "KernelIpcChannel reports connected mode-switch error")) {
    return false;
  }
  if (!Assert(channel.IsConnected(), "KernelIpcChannel connected state")) {
    return false;
  }

  if (!Assert(channel.Send(KernelRequest{KernelOpcode::kReadDirectory, "", 0, 0, 0}, &response),
              "KernelIpcChannel send read directory")) {
    return false;
  }
  if (!Assert(response.success && response.directory_entries.size() == 1,
              "KernelIpcChannel read directory response")) {
    return false;
  }

  if (!Assert(channel.Disconnect(), "KernelIpcChannel disconnect")) {
    return false;
  }
  if (!Assert(!channel.IsConnected(), "KernelIpcChannel disconnected state")) {
    return false;
  }

  return true;
}

bool TestKernelTransportScaffold(const std::filesystem::path& image_path) {
  KernelTransport transport;

  if (!Assert(transport.GetMode() == KernelTransport::Mode::kLoopback,
              "KernelTransport default mode is loopback")) {
    return false;
  }

  KernelResponse response;
  if (!Assert(!transport.Send(KernelRequest{KernelOpcode::kReadDirectory, "", 0, 0, 0}, &response),
              "KernelTransport send fails before connect")) {
    return false;
  }

  if (!Assert(transport.Connect(image_path.string()), "KernelTransport loopback connect")) {
    return false;
  }
  if (!Assert(!transport.SetMode(KernelTransport::Mode::kDevice),
              "KernelTransport rejects mode switch while connected")) {
    return false;
  }
  if (!Assert(transport.LastError().find("while connected") != std::string::npos,
              "KernelTransport mode switch error is explicit")) {
    return false;
  }
  if (!Assert(transport.Send(KernelRequest{KernelOpcode::kReadDirectory, "", 0, 0, 0}, &response),
              "KernelTransport loopback send")) {
    return false;
  }
  if (!Assert(response.success && response.directory_entries.size() == 1,
              "KernelTransport loopback response")) {
    return false;
  }
  if (!Assert(transport.Disconnect(), "KernelTransport loopback disconnect")) {
    return false;
  }

  if (!Assert(transport.SetMode(KernelTransport::Mode::kDevice),
              "KernelTransport switches to device mode before connect")) {
    return false;
  }
  if (!Assert(!transport.Connect(image_path.string()), "KernelTransport device connect not available in scaffold")) {
    return false;
  }
  if (!Assert(transport.LastError().find("not available") != std::string::npos,
              "KernelTransport device connect error is explicit")) {
    return false;
  }

  if (!Assert(transport.SetMode(KernelTransport::Mode::kLoopback),
              "KernelTransport reset to loopback after failed device connect")) {
    return false;
  }
  if (!Assert(transport.Connect(image_path.string()), "KernelTransport reconnect loopback")) {
    return false;
  }
  if (!Assert(transport.Disconnect(), "KernelTransport disconnect succeeds after resetting loopback mode")) {
    return false;
  }

  if (!Assert(transport.SetMode(KernelTransport::Mode::kDevice),
              "KernelTransport switches to device mode when disconnected")) {
    return false;
  }
  if (!Assert(!transport.Connect(image_path.string()), "KernelTransport device connect not available in scaffold")) {
    return false;
  }
  if (!Assert(transport.LastError().find("not available") != std::string::npos,
              "KernelTransport device connect error is explicit")) {
    return false;
  }

  std::vector<std::uint8_t> frame;
  const KernelRequest request{KernelOpcode::kReadFile, "HELLO.PRG", 42, 7, 128};
  if (!Assert(transport.BuildDeviceFrame(request, &frame), "KernelTransport builds device request frame")) {
    return false;
  }
  if (!Assert(frame.size() == (sizeof(std::uint32_t) + sizeof(std::uint64_t) + sizeof(std::uint64_t) +
                                 sizeof(std::uint32_t) + sizeof(std::uint32_t) + request.windows_name.size()),
              "KernelTransport frame size matches header plus path")) {
    return false;
  }

  std::uint32_t probe_opcode = 0;
  std::uint64_t probe_handle = 0;
  std::uint64_t probe_offset = 0;
  std::uint32_t probe_size = 0;
  std::uint32_t probe_path_bytes = 0;
  std::memcpy(&probe_opcode, frame.data(), sizeof(probe_opcode));
  std::memcpy(&probe_handle, frame.data() + sizeof(std::uint32_t), sizeof(probe_handle));
  std::memcpy(&probe_offset, frame.data() + sizeof(std::uint32_t) + sizeof(std::uint64_t), sizeof(probe_offset));
  std::memcpy(&probe_size,
              frame.data() + sizeof(std::uint32_t) + sizeof(std::uint64_t) + sizeof(std::uint64_t),
              sizeof(probe_size));
  std::memcpy(&probe_path_bytes,
              frame.data() + sizeof(std::uint32_t) + sizeof(std::uint64_t) + sizeof(std::uint64_t) +
                  sizeof(std::uint32_t),
              sizeof(probe_path_bytes));
  if (!Assert(probe_opcode == static_cast<std::uint32_t>(KernelOpcode::kReadFile),
              "KernelTransport frame opcode encoded")) {
    return false;
  }
  if (!Assert(probe_handle == 42 && probe_offset == 7 && probe_size == 128,
              "KernelTransport frame numeric payload encoded")) {
    return false;
  }
  if (!Assert(probe_path_bytes == request.windows_name.size(),
              "KernelTransport frame path length encoded")) {
    return false;
  }
  const std::string encoded_path(
      reinterpret_cast<const char*>(frame.data() + sizeof(std::uint32_t) + sizeof(std::uint64_t) +
                                    sizeof(std::uint64_t) + sizeof(std::uint32_t) + sizeof(std::uint32_t)),
      probe_path_bytes);
  if (!Assert(encoded_path == request.windows_name, "KernelTransport frame path encoded")) {
    return false;
  }

  KernelResponse parsed;
  if (!Assert(transport.ParseDeviceFrame({}, &parsed), "KernelTransport parses empty response frame")) {
    return false;
  }
  if (!Assert(!parsed.success && parsed.error.find("empty") != std::string::npos,
              "KernelTransport empty response frame is explicit")) {
    return false;
  }

  if (!Assert(transport.ParseDeviceFrame(std::vector<std::uint8_t>{1, 2, 3}, &parsed),
              "KernelTransport parses truncated response frame")) {
    return false;
  }
  if (!Assert(!parsed.success && parsed.error.find("truncated") != std::string::npos,
              "KernelTransport truncated response frame is explicit")) {
    return false;
  }

  const std::string payload_text = "HELLO";
  const std::string error_text = "DEVICE_ERR";
  std::vector<std::uint8_t> response_frame(KernelTransport::kDeviceResponseHeaderSize +
                                           payload_text.size() + error_text.size(),
                                           0);
  const std::uint32_t success = 0;
  const std::uint32_t payload_bytes = static_cast<std::uint32_t>(payload_text.size());
  const std::uint64_t response_handle = 99;
  const std::uint32_t error_bytes = static_cast<std::uint32_t>(error_text.size());
  std::memcpy(response_frame.data() + 0, &success, sizeof(success));
  std::memcpy(response_frame.data() + sizeof(std::uint32_t), &payload_bytes, sizeof(payload_bytes));
  std::memcpy(response_frame.data() + sizeof(std::uint32_t) + sizeof(std::uint32_t),
              &response_handle,
              sizeof(response_handle));
  std::memcpy(response_frame.data() + sizeof(std::uint32_t) + sizeof(std::uint32_t) +
                  sizeof(std::uint64_t),
              &error_bytes,
              sizeof(error_bytes));
  std::memcpy(response_frame.data() + KernelTransport::kDeviceResponseHeaderSize,
              payload_text.data(),
              payload_text.size());
  std::memcpy(response_frame.data() + KernelTransport::kDeviceResponseHeaderSize + payload_text.size(),
              error_text.data(),
              error_text.size());

  if (!Assert(transport.ParseDeviceFrame(response_frame, &parsed),
              "KernelTransport parses populated response frame")) {
    return false;
  }
  if (!Assert(!parsed.success && parsed.handle == response_handle,
              "KernelTransport parsed success flag and handle")) {
    return false;
  }
  if (!Assert(std::string(parsed.data.begin(), parsed.data.end()) == payload_text,
              "KernelTransport parsed payload bytes")) {
    return false;
  }
  if (!Assert(parsed.error == error_text, "KernelTransport parsed error text")) {
    return false;
  }

  FakeDeviceIoApi fake_api;
  KernelTransport device_transport;
  if (!Assert(device_transport.SetMode(KernelTransport::Mode::kDevice),
              "KernelTransport test instance switches to device mode")) {
    return false;
  }
  if (!Assert(device_transport.SetDeviceIoApiForTesting(&fake_api),
              "KernelTransport accepts injected device IO API")) {
    return false;
  }
  if (!Assert(device_transport.Connect(image_path.string()),
              "KernelTransport connects with injected device IO API")) {
    return false;
  }
  if (!Assert(fake_api.open_calls == 1, "KernelTransport invokes device open once")) {
    return false;
  }
  if (!Assert(!device_transport.SetDeviceIoApiForTesting(&fake_api),
              "KernelTransport rejects changing device API while connected")) {
    return false;
  }

  const std::string ioctl_payload = "DATA";
  std::vector<std::uint8_t> ioctl_response(KernelTransport::kDeviceResponseHeaderSize +
                                           ioctl_payload.size(),
                                           0);
  const std::uint32_t ioctl_success = 1;
  const std::uint32_t ioctl_payload_bytes = static_cast<std::uint32_t>(ioctl_payload.size());
  const std::uint64_t ioctl_handle = 123;
  const std::uint32_t ioctl_error_bytes = 0;
  std::memcpy(ioctl_response.data() + 0, &ioctl_success, sizeof(ioctl_success));
  std::memcpy(ioctl_response.data() + sizeof(std::uint32_t),
              &ioctl_payload_bytes,
              sizeof(ioctl_payload_bytes));
  std::memcpy(ioctl_response.data() + sizeof(std::uint32_t) + sizeof(std::uint32_t),
              &ioctl_handle,
              sizeof(ioctl_handle));
  std::memcpy(ioctl_response.data() + sizeof(std::uint32_t) + sizeof(std::uint32_t) +
                  sizeof(std::uint64_t),
              &ioctl_error_bytes,
              sizeof(ioctl_error_bytes));
  std::memcpy(ioctl_response.data() + KernelTransport::kDeviceResponseHeaderSize,
              ioctl_payload.data(),
              ioctl_payload.size());
  fake_api.next_response_frame = ioctl_response;

  KernelResponse ioctl_result;
  if (!Assert(device_transport.Send(KernelRequest{KernelOpcode::kReadFile, "HELLO.PRG", 1, 0, 4},
                                  &ioctl_result),
              "KernelTransport device send succeeds with injected IOCTL response")) {
    return false;
  }
  if (!Assert(fake_api.ioctl_calls == 1, "KernelTransport invokes IOCTL once")) {
    return false;
  }
  if (!Assert(!fake_api.last_request_frame.empty(), "KernelTransport passes encoded request frame to IOCTL")) {
    return false;
  }
  if (!Assert(ioctl_result.success && ioctl_result.handle == ioctl_handle,
              "KernelTransport parses IOCTL success and handle")) {
    return false;
  }
  if (!Assert(std::string(ioctl_result.data.begin(), ioctl_result.data.end()) == ioctl_payload,
              "KernelTransport parses IOCTL payload data")) {
    return false;
  }

  if (!Assert(device_transport.Disconnect(), "KernelTransport disconnects injected device API")) {
    return false;
  }
  if (!Assert(fake_api.close_calls == 1, "KernelTransport invokes device close once")) {
    return false;
  }

  FakeDeviceIoApi failing_api;
  failing_api.ioctl_ok = false;
  failing_api.ioctl_error = "fake ioctl failure";
  KernelTransport failing_transport;
  if (!Assert(failing_transport.SetMode(KernelTransport::Mode::kDevice),
              "KernelTransport failing instance switches to device mode")) {
    return false;
  }
  if (!Assert(failing_transport.SetDeviceIoApiForTesting(&failing_api),
              "KernelTransport failing instance accepts injected API")) {
    return false;
  }
  if (!Assert(failing_transport.Connect(image_path.string()),
              "KernelTransport failing instance connects")) {
    return false;
  }
  KernelResponse failing_result;
  if (!Assert(!failing_transport.Send(KernelRequest{KernelOpcode::kReadDirectory, "", 0, 0, 0},
                                   &failing_result),
              "KernelTransport surfaces IOCTL failure")) {
    return false;
  }
  if (!Assert(failing_result.error == "fake ioctl failure",
              "KernelTransport returns IOCTL failure text")) {
    return false;
  }
  if (!Assert(failing_transport.Disconnect(), "KernelTransport failing instance disconnects")) {
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
  ok = ok && TestWinFspCallbacksBridge(image_path);
  ok = ok && TestWinFspNativeBridgeScaffold(image_path);
  ok = ok && TestWinFspNativeBridgeProviderInjection(image_path);
  ok = ok && TestWinFspDefaultNativeApiContract(image_path);
  ok = ok && TestMountBackendFactory(image_path);
  ok = ok && TestKernelMountManagerScaffold();
  ok = ok && TestKernelReadOnlyFilesystemScaffold(image_path);
  ok = ok && TestKernelUserBridgeScaffold(image_path);
  ok = ok && TestKernelIpcChannelScaffold(image_path);
  ok = ok && TestKernelTransportScaffold(image_path);
  ok = ok && TestWinFspAdapterScaffold(image_path);
  ok = ok && TestWinFspRuntimeScaffold(image_path);

  std::error_code ec;
  std::filesystem::remove(image_path, ec);

  if (!ok) {
    return 1;
  }

  std::cout << "All tests passed\n";
  return 0;
}
