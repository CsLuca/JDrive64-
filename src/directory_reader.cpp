#include "jdrive64/directory_reader.hpp"

#include <array>
#include <utility>

#include "jdrive64/d64_reader.hpp"
#include "jdrive64/petscii_converter.hpp"

namespace jdrive64 {

namespace {

constexpr std::uint8_t kDirStartTrack = 18;
constexpr std::uint8_t kDirStartSector = 1;
constexpr std::size_t kEntrySize = 32;

bool IsValidDirectoryNextPointer(const D64Reader& reader,
                                 std::uint8_t next_track,
                                 std::uint8_t next_sector) {
  if (next_track == 0) {
    return true;
  }
  if (next_track < D64Reader::kMinTrack || next_track > D64Reader::kMaxTrack) {
    return false;
  }
  return next_sector < reader.SectorsPerTrack(next_track);
}

bool IsSupportedFileType(std::uint8_t file_type) {
  const auto kind = static_cast<std::uint8_t>(file_type & 0x07);
  return kind <= 4;
}

}  // namespace

bool DirectoryReader::Load(D64Reader& reader) {
  entries_.clear();
  last_error_.clear();

  std::uint8_t current_track = kDirStartTrack;
  std::uint8_t current_sector = kDirStartSector;

  std::array<std::uint8_t, D64Reader::kSectorSize> sector{};

  int sector_chain_guard = 0;
  while (current_track != 0) {
    ++sector_chain_guard;
    if (sector_chain_guard > 64) {
      last_error_ = "Directory sector chain too long";
      return false;
    }

    if (!reader.ReadSector(current_track, current_sector, sector.data())) {
      last_error_ = reader.LastError();
      return false;
    }

    for (std::size_t offset = 2; offset + kEntrySize <= D64Reader::kSectorSize; offset += kEntrySize) {
      const std::uint8_t file_type = sector[offset + 2];
      if ((file_type & 0x0F) == 0 || (file_type & 0x80) == 0) {
        continue;
      }

      if (!IsSupportedFileType(file_type)) {
        continue;
      }

      const std::uint8_t start_track = sector[offset + 3];
      const std::uint8_t start_sector = sector[offset + 4];
      if (start_track != 0 &&
          (start_track < D64Reader::kMinTrack || start_track > D64Reader::kMaxTrack ||
           start_sector >= reader.SectorsPerTrack(start_track))) {
        continue;
      }

      DirectoryEntry entry;
      entry.file_type = file_type;
      entry.start_track = start_track;
      entry.start_sector = start_sector;
      entry.name = PetsciiToUtf8(&sector[offset + 5], 16);
      entry.extension = FileTypeToExtension(file_type);
      entry.size_blocks = static_cast<std::uint16_t>(sector[offset + 30]) |
                          (static_cast<std::uint16_t>(sector[offset + 31]) << 8);

      entries_.push_back(std::move(entry));
    }

    const auto next_track = sector[0];
    const auto next_sector = sector[1];
    if (!IsValidDirectoryNextPointer(reader, next_track, next_sector)) {
      last_error_ = "Invalid directory sector chain pointer";
      return false;
    }

    current_track = next_track;
    current_sector = next_sector;
  }

  return true;
}

const std::vector<DirectoryEntry>& DirectoryReader::Entries() const { return entries_; }

const std::string& DirectoryReader::LastError() const { return last_error_; }

std::string DirectoryReader::FileTypeToExtension(std::uint8_t file_type) {
  switch (file_type & 0x07) {
    case 0:
      return "DEL";
    case 1:
      return "SEQ";
    case 2:
      return "PRG";
    case 3:
      return "USR";
    case 4:
      return "REL";
    default:
      return "UNK";
  }
}

}  // namespace jdrive64
