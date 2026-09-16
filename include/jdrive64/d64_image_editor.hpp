#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "jdrive64/d64_reader.hpp"

namespace jdrive64 {

class D64ImageEditor {
 public:
  bool Open(const std::string& image_path);

  bool AddFile(const std::string& host_file_path, const std::string& windows_name);
  bool DeleteFile(const std::string& windows_name);
  bool RenameFile(const std::string& old_windows_name, const std::string& new_windows_name);

  const std::string& LastError() const;

 private:
  struct EntryLocation {
    std::uint8_t dir_track = 0;
    std::uint8_t dir_sector = 0;
    std::size_t offset = 0;
    std::uint8_t file_type = 0;
    std::uint8_t start_track = 0;
    std::uint8_t start_sector = 0;
  };

  bool LoadBam(std::array<std::uint8_t, D64Reader::kSectorSize>* bam);
  bool SaveBam(const std::array<std::uint8_t, D64Reader::kSectorSize>& bam);
  bool ReadSector(std::uint8_t track,
                  std::uint8_t sector,
                  std::array<std::uint8_t, D64Reader::kSectorSize>* data);
  bool WriteSector(std::uint8_t track,
                   std::uint8_t sector,
                   const std::array<std::uint8_t, D64Reader::kSectorSize>& data);

  bool FindEntryByName(const std::string& windows_name,
                       EntryLocation* location,
                       std::array<std::uint8_t, D64Reader::kSectorSize>* sector_data);
  bool FindFreeDirectoryEntry(EntryLocation* location,
                              std::array<std::uint8_t, D64Reader::kSectorSize>* sector_data);

  bool ParseWindowsName(const std::string& windows_name,
                        std::string* base_name,
                        std::string* ext_upper) const;
  std::uint8_t ExtensionToFileType(const std::string& ext_upper) const;
  std::string FileTypeToExtension(std::uint8_t file_type) const;
  std::string NormalizeBaseName(const std::string& value) const;
  std::string ToUpper(std::string value) const;
  std::string DirectoryEntryToWindowsName(const std::array<std::uint8_t, D64Reader::kSectorSize>& sector,
                                          std::size_t offset) const;

  bool IsSectorFree(const std::array<std::uint8_t, D64Reader::kSectorSize>& bam,
                    std::uint8_t track,
                    std::uint8_t sector) const;
  void MarkSectorUsed(std::array<std::uint8_t, D64Reader::kSectorSize>* bam,
                      std::uint8_t track,
                      std::uint8_t sector);
  void MarkSectorFree(std::array<std::uint8_t, D64Reader::kSectorSize>* bam,
                      std::uint8_t track,
                      std::uint8_t sector);

  bool CollectFileChain(std::uint8_t start_track,
                        std::uint8_t start_sector,
                        std::vector<std::pair<std::uint8_t, std::uint8_t>>* chain);

  bool OpenHostFile(const std::string& host_file_path, std::vector<std::uint8_t>* data);

  D64Reader reader_;
  std::fstream writer_;
  std::string last_error_;
};

}  // namespace jdrive64
