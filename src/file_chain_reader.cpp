#include "jdrive64/file_chain_reader.hpp"

#include <array>
#include <set>
#include <utility>

#include "jdrive64/d64_reader.hpp"
#include "jdrive64/directory_reader.hpp"

namespace jdrive64 {

FileChainReader::FileChainReader(D64Reader& reader) : reader_(reader) {}

std::vector<std::uint8_t> FileChainReader::ReadFile(const DirectoryEntry& entry) {
  last_error_.clear();
  std::vector<std::uint8_t> out;

  if (entry.start_track == 0) {
    return out;
  }

  std::array<std::uint8_t, D64Reader::kSectorSize> sector{};
  std::set<std::pair<std::uint8_t, std::uint8_t>> visited;

  std::uint8_t track = entry.start_track;
  std::uint8_t sec = entry.start_sector;

  while (track != 0) {
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
      out.insert(out.end(), sector.begin() + 2, sector.begin() + 2 + payload_size);
      break;
    }

    out.insert(out.end(), sector.begin() + 2, sector.end());
    track = next_track;
    sec = next_sector;
  }

  return out;
}

const std::string& FileChainReader::LastError() const { return last_error_; }

}  // namespace jdrive64
