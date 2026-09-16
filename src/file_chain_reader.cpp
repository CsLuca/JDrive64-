#include "jdrive64/file_chain_reader.hpp"

#include <array>
#include <set>
#include <sstream>
#include <utility>

#include "jdrive64/d64_reader.hpp"
#include "jdrive64/disk_catalog.hpp"
#include "jdrive64/directory_reader.hpp"

namespace jdrive64 {

FileChainReader::FileChainReader(D64Reader& reader) : reader_(reader) {}

std::vector<std::uint8_t> FileChainReader::ReadFile(const DirectoryEntry& entry) {
  last_error_.clear();
  std::vector<std::uint8_t> out;

  constexpr std::size_t kMaxChainSectors = 1024;
  constexpr std::size_t kMaxOutputBytes = 16 * 1024 * 1024;

  if (entry.start_track == 0) {
    return out;
  }

  if (entry.start_track < D64Reader::kMinTrack || entry.start_track > D64Reader::kMaxTrack) {
    last_error_ = "Invalid start track in directory entry";
    return {};
  }
  if (entry.start_sector >= reader_.SectorsPerTrack(entry.start_track)) {
    last_error_ = "Invalid start sector in directory entry";
    return {};
  }

  std::array<std::uint8_t, D64Reader::kSectorSize> sector{};
  std::set<std::pair<std::uint8_t, std::uint8_t>> visited;

  std::uint8_t track = entry.start_track;
  std::uint8_t sec = entry.start_sector;
  std::size_t chain_count = 0;

  while (track != 0) {
    ++chain_count;
    if (chain_count > kMaxChainSectors) {
      last_error_ = "File sector chain exceeds safety limit";
      return {};
    }

    if (track < D64Reader::kMinTrack || track > D64Reader::kMaxTrack) {
      std::ostringstream oss;
      oss << "Invalid track in file chain: " << static_cast<int>(track);
      last_error_ = oss.str();
      return {};
    }
    if (sec >= reader_.SectorsPerTrack(track)) {
      std::ostringstream oss;
      oss << "Invalid sector in file chain: " << static_cast<int>(sec) << " on track "
          << static_cast<int>(track);
      last_error_ = oss.str();
      return {};
    }

    const auto key = std::make_pair(track, sec);
    if (!visited.insert(key).second) {
      last_error_ = "Detected loop in file sector chain";
      return {};
    }

    if (!reader_.ReadSector(track, sec, sector.data())) {
      last_error_ = reader_.LastError();
      return {};
    }

    const std::uint8_t next_track = sector[0];
    const std::uint8_t next_sector = sector[1];

    if (next_track == 0) {
      const std::size_t used_bytes = next_sector;
      if (used_bytes < 2 || used_bytes > D64Reader::kSectorSize) {
        last_error_ = "Invalid last sector byte count";
        return {};
      }

      const auto payload_size = used_bytes - 2;
      if (out.size() + payload_size > kMaxOutputBytes) {
        last_error_ = "File output exceeds safety limit";
        return {};
      }
      out.insert(out.end(), sector.begin() + 2, sector.begin() + 2 + payload_size);
      break;
    }

    if (next_track < D64Reader::kMinTrack || next_track > D64Reader::kMaxTrack ||
        next_sector >= reader_.SectorsPerTrack(next_track)) {
      std::ostringstream oss;
      oss << "Invalid next pointer in file chain: " << static_cast<int>(next_track) << "/"
          << static_cast<int>(next_sector);
      last_error_ = oss.str();
      return {};
    }

    if (out.size() + (D64Reader::kSectorSize - 2) > kMaxOutputBytes) {
      last_error_ = "File output exceeds safety limit";
      return {};
    }
    out.insert(out.end(), sector.begin() + 2, sector.end());
    track = next_track;
    sec = next_sector;
  }

  return out;
}

std::vector<std::uint8_t> FileChainReader::ReadFile(const CatalogFile& file) {
  DirectoryEntry entry;
  entry.file_type = file.file_type;
  entry.start_track = file.start_track;
  entry.start_sector = file.start_sector;
  entry.size_blocks = file.size_blocks;
  entry.name = file.display_name;
  entry.extension = file.extension;
  return ReadFile(entry);
}

const std::string& FileChainReader::LastError() const { return last_error_; }

}  // namespace jdrive64
