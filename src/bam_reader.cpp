#include "jdrive64/bam_reader.hpp"

#include <array>

#include "jdrive64/d64_reader.hpp"
#include "jdrive64/petscii_converter.hpp"

namespace jdrive64 {

bool BAMReader::Load(D64Reader& reader) {
  last_error_.clear();

  std::array<std::uint8_t, D64Reader::kSectorSize> sector{};
  if (!reader.ReadSector(18, 0, sector.data())) {
    last_error_ = reader.LastError();
    return false;
  }

  free_blocks_ = 0;
  for (std::uint8_t track = D64Reader::kMinTrack; track <= D64Reader::kMaxTrack; ++track) {
    const std::size_t entry_offset = 4 + static_cast<std::size_t>(track - 1) * 4;
    free_blocks_ += sector[entry_offset];
  }

  disk_name_ = PetsciiToUtf8(&sector[0x90], 16);
  disk_id_ = PetsciiToUtf8(&sector[0xA2], 2);
  dos_type_ = PetsciiToUtf8(&sector[0xA5], 2);

  return true;
}

std::uint16_t BAMReader::FreeBlocks() const { return free_blocks_; }

const std::string& BAMReader::DiskName() const { return disk_name_; }

const std::string& BAMReader::DiskId() const { return disk_id_; }

const std::string& BAMReader::DosType() const { return dos_type_; }

const std::string& BAMReader::LastError() const { return last_error_; }

}  // namespace jdrive64
