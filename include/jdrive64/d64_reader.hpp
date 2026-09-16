#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace jdrive64 {

class D64Reader {
 public:
  static constexpr std::size_t kSectorSize = 256;
  static constexpr std::uint8_t kMinTrack = 1;
  static constexpr std::uint8_t kMaxTrack = 35;

  bool Open(const std::string& filename);
  bool ReadSector(std::uint8_t track, std::uint8_t sector, std::uint8_t* data);
  std::uint32_t TrackSectorToOffset(std::uint8_t track, std::uint8_t sector) const;

  std::uint8_t SectorsPerTrack(std::uint8_t track) const;
  bool IsOpen() const;
  const std::string& LastError() const;
  std::uint64_t ImageSize() const;

 private:
  bool IsValidTrack(std::uint8_t track) const;

  std::ifstream file_;
  std::uint64_t image_size_ = 0;
  std::string last_error_;

  static const std::array<std::uint32_t, 36> kTrackStart;
};

}  // namespace jdrive64
