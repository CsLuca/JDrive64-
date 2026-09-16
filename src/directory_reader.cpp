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

}  // namespace

bool DirectoryReader::Load(D64Reader& reader) {
  entries_.clear();
  last_error_.clear();

  std::uint8_t current_track = kDirStartTrack;
  std::uint8_t current_sector = kDirStartSector;

  std::array<std::uint8_t, D64Reader::kSectorSize> sector{};

  while (current_track != 0) {
    if (!reader.ReadSector(current_track, current_sector, sector.data())) {
      last_error_ = reader.LastError();
      return false;
    }

    for (std::size_t offset = 2; offset < D64Reader::kSectorSize; offset += kEntrySize) {
      const std::uint8_t file_type = sector[offset + 2];
      if ((file_type & 0x0F) == 0) {
        continue;
      }

      DirectoryEntry entry;
      entry.file_type = file_type;
      entry.start_track = sector[offset + 3];
      entry.start_sector = sector[offset + 4];
      entry.name = PetsciiToUtf8(&sector[offset + 5], 16);
      entry.extension = FileTypeToExtension(file_type);
      entry.size_blocks = static_cast<std::uint16_t>(sector[offset + 30]) |
                          (static_cast<std::uint16_t>(sector[offset + 31]) << 8);

      entries_.push_back(std::move(entry));
    }

    current_track = sector[0];
    current_sector = sector[1];
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
      return "BIN";
  }
}

}  // namespace jdrive64
