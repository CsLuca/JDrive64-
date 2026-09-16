#include "jdrive64/d64_reader.hpp"

#include <algorithm>
#include <vector>

#include "jdrive64/sector_cache.hpp"

namespace jdrive64 {

const std::array<std::uint32_t, 36> D64Reader::kTrackStart = {
    0,
    0,
    21,
    42,
    63,
    84,
    105,
    126,
    147,
    168,
    189,
    210,
    231,
    252,
    273,
    294,
    315,
    336,
    357,
    376,
    395,
    414,
    433,
    452,
    471,
    489,
    507,
    525,
    543,
    561,
    579,
    596,
    613,
    630,
    647,
    664,
};

bool D64Reader::Open(const std::string& filename) {
  last_error_.clear();
  image_size_ = 0;

  file_.close();
  file_.clear();
  file_.open(filename, std::ios::binary);
  if (!file_) {
    last_error_ = "Cannot open D64 file";
    return false;
  }

  file_.seekg(0, std::ios::end);
  if (!file_) {
    last_error_ = "Cannot seek D64 file";
    return false;
  }

  const auto end_pos = file_.tellg();
  if (end_pos < 0) {
    last_error_ = "Invalid D64 file size";
    return false;
  }

  image_size_ = static_cast<std::uint64_t>(end_pos);
  file_.seekg(0, std::ios::beg);

  constexpr std::uint64_t kD64Size = 174848;
  constexpr std::uint64_t kD64WithErrorInfoSize = 175531;
  if (image_size_ != kD64Size && image_size_ != kD64WithErrorInfoSize) {
    last_error_ = "Unsupported D64 image size";
    return false;
  }

  return true;
}

bool D64Reader::ReadSector(std::uint8_t track, std::uint8_t sector, std::uint8_t* data) {
  last_error_.clear();

  if (!IsOpen()) {
    last_error_ = "D64 file is not open";
    return false;
  }

  if (!IsValidTrack(track)) {
    last_error_ = "Invalid track";
    return false;
  }

  if (sector >= SectorsPerTrack(track)) {
    last_error_ = "Invalid sector";
    return false;
  }

  if (sector_cache_ != nullptr) {
    std::vector<std::uint8_t> cached;
    if (sector_cache_->Get(track, sector, &cached) && cached.size() == kSectorSize) {
      std::copy(cached.begin(), cached.end(), data);
      return true;
    }
  }

  const auto offset = TrackSectorToOffset(track, sector);
  if (offset + kSectorSize > image_size_) {
    last_error_ = "Sector offset out of image bounds";
    return false;
  }

  file_.clear();
  file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!file_) {
    last_error_ = "Seek failed while reading sector";
    return false;
  }

  file_.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(kSectorSize));
  if (!file_) {
    last_error_ = "Read failed while reading sector";
    return false;
  }

  if (sector_cache_ != nullptr) {
    sector_cache_->Put(track, sector, data, kSectorSize);
  }

  return true;
}

std::uint32_t D64Reader::TrackSectorToOffset(std::uint8_t track, std::uint8_t sector) const {
  return (kTrackStart[track] + sector) * static_cast<std::uint32_t>(kSectorSize);
}

std::uint8_t D64Reader::SectorsPerTrack(std::uint8_t track) const {
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

bool D64Reader::IsOpen() const { return file_.is_open(); }

const std::string& D64Reader::LastError() const { return last_error_; }

std::uint64_t D64Reader::ImageSize() const { return image_size_; }

void D64Reader::SetSectorCache(SectorCache* cache) { sector_cache_ = cache; }

bool D64Reader::IsValidTrack(std::uint8_t track) const {
  return track >= kMinTrack && track <= kMaxTrack;
}

}  // namespace jdrive64
