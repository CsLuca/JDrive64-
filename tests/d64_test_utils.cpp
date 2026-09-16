#include "d64_test_utils.hpp"

#include <array>
#include <fstream>

namespace jdrive64::tests {

namespace {

std::size_t BamTrackOffset(std::uint8_t track) {
  return 4 + static_cast<std::size_t>(track - 1) * 4;
}

void SetSectorBit(std::array<std::uint8_t, D64Reader::kSectorSize>& bam,
                  std::uint8_t track,
                  std::uint8_t sector,
                  bool free_value) {
  const std::size_t entry = BamTrackOffset(track);
  const std::size_t byte_index = 1 + static_cast<std::size_t>(sector / 8);
  const std::uint8_t bit = static_cast<std::uint8_t>(1U << (sector % 8));
  if (free_value) {
    bam[entry + byte_index] = static_cast<std::uint8_t>(bam[entry + byte_index] | bit);
  } else {
    bam[entry + byte_index] = static_cast<std::uint8_t>(bam[entry + byte_index] & ~bit);
  }
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

void FillPetsciiName(std::array<std::uint8_t, D64Reader::kSectorSize>& s,
                     std::size_t offset,
                     const std::string& name,
                     std::size_t max_len) {
  for (std::size_t i = 0; i < max_len; ++i) {
    s[offset + i] = (i < name.size()) ? static_cast<std::uint8_t>(name[i]) : 0xA0;
  }
}

void SaveImage(const std::filesystem::path& path, const D64Image& image) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(image.bytes.data()),
            static_cast<std::streamsize>(image.bytes.size()));
}

void BuildCommonBam(D64Image& image, std::uint8_t free_t1, std::uint8_t free_t2, const std::string& label) {
  std::array<std::uint8_t, D64Reader::kSectorSize> bam{};

  for (std::uint8_t track = D64Reader::kMinTrack; track <= D64Reader::kMaxTrack; ++track) {
    const auto sectors = SectorsPerTrack(track);
    for (std::uint8_t sector = 0; sector < sectors; ++sector) {
      SetSectorBit(bam, track, sector, true);
    }
  }

  bam[4 + (1 - 1) * 4] = free_t1;
  bam[4 + (2 - 1) * 4] = free_t2;
  FillPetsciiName(bam, 0x90, label, 16);
  bam[0xA2] = 'A';
  bam[0xA3] = 'B';
  bam[0xA5] = '2';
  bam[0xA6] = 'A';
  WriteSector(image, 18, 0, bam);
}

void MarkUsedInBam(D64Image& image, std::uint8_t track, std::uint8_t sector) {
  std::array<std::uint8_t, D64Reader::kSectorSize> bam{};
  const auto bam_offset = TrackSectorToOffset(18, 0);
  for (std::size_t i = 0; i < bam.size(); ++i) {
    bam[i] = image.bytes[bam_offset + i];
  }

  SetSectorBit(bam, track, sector, false);

  for (std::size_t i = 0; i < bam.size(); ++i) {
    image.bytes[bam_offset + i] = bam[i];
  }
}

}  // namespace

std::size_t TrackSectorToOffset(std::uint8_t track, std::uint8_t sector) {
  std::size_t sectors_before = 0;
  for (std::uint8_t t = 1; t < track; ++t) {
    sectors_before += SectorsPerTrack(t);
  }
  return (sectors_before + sector) * D64Reader::kSectorSize;
}

void WriteSector(D64Image& image,
                 std::uint8_t track,
                 std::uint8_t sector,
                 const std::array<std::uint8_t, D64Reader::kSectorSize>& sector_data) {
  const auto offset = TrackSectorToOffset(track, sector);
  for (std::size_t i = 0; i < D64Reader::kSectorSize; ++i) {
    image.bytes[offset + i] = sector_data[i];
  }
}

D64Image MakeBlankImage(bool with_error_info) {
  D64Image image;
  image.bytes.resize(with_error_info ? 175531 : 174848, 0);
  return image;
}

GoldenPaths CreateGoldenCorpus(const std::filesystem::path& root) {
  std::error_code ec;
  std::filesystem::create_directories(root, ec);

  GoldenPaths paths;
  paths.root = root;
  paths.valid_small = root / "valid_small.d64";
  paths.valid_multi = root / "valid_multi.d64";
  paths.valid_errorinfo = root / "valid_errorinfo.d64";
  paths.bad_size = root / "bad_size.d64";
  paths.bad_dir_pointer = root / "bad_dir_pointer.d64";
  paths.bad_file_loop = root / "bad_file_loop.d64";
  paths.bad_file_next_pointer = root / "bad_file_next_pointer.d64";

  {
    D64Image image = MakeBlankImage(false);
    BuildCommonBam(image, 10, 20, "TEST SMALL");

    std::array<std::uint8_t, D64Reader::kSectorSize> dir{};
    dir[0] = 0;
    dir[1] = 0;
    dir[2 + 2] = 0x82;
    dir[2 + 3] = 1;
    dir[2 + 4] = 0;
    FillPetsciiName(dir, 2 + 5, "HELLO", 16);
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

    MarkUsedInBam(image, 18, 0);
    MarkUsedInBam(image, 18, 1);
    MarkUsedInBam(image, 1, 0);

    SaveImage(paths.valid_small, image);
  }

  {
    D64Image image = MakeBlankImage(false);
    BuildCommonBam(image, 30, 40, "TEST MULTI");

    std::array<std::uint8_t, D64Reader::kSectorSize> dir{};
    dir[0] = 0;
    dir[1] = 0;

    dir[2 + 2] = 0x82;
    dir[2 + 3] = 1;
    dir[2 + 4] = 0;
    FillPetsciiName(dir, 2 + 5, "ONE", 16);
    dir[2 + 30] = 1;

    dir[34 + 2] = 0x81;
    dir[34 + 3] = 1;
    dir[34 + 4] = 1;
    FillPetsciiName(dir, 34 + 5, "DATA", 16);
    dir[34 + 30] = 1;

    WriteSector(image, 18, 1, dir);

    std::array<std::uint8_t, D64Reader::kSectorSize> s1{};
    s1[0] = 0;
    s1[1] = 5;
    s1[2] = 'O';
    s1[3] = 'N';
    s1[4] = 'E';
    WriteSector(image, 1, 0, s1);

    std::array<std::uint8_t, D64Reader::kSectorSize> s2{};
    s2[0] = 0;
    s2[1] = 6;
    s2[2] = 'D';
    s2[3] = 'A';
    s2[4] = 'T';
    s2[5] = 'A';
    WriteSector(image, 1, 1, s2);

    MarkUsedInBam(image, 18, 0);
    MarkUsedInBam(image, 18, 1);
    MarkUsedInBam(image, 1, 0);
    MarkUsedInBam(image, 1, 1);

    SaveImage(paths.valid_multi, image);
  }

  {
    D64Image image = MakeBlankImage(true);
    BuildCommonBam(image, 8, 9, "ERRINFO");

    std::array<std::uint8_t, D64Reader::kSectorSize> dir{};
    dir[0] = 0;
    dir[1] = 0;
    WriteSector(image, 18, 1, dir);

    MarkUsedInBam(image, 18, 0);
    MarkUsedInBam(image, 18, 1);

    SaveImage(paths.valid_errorinfo, image);
  }

  {
    std::vector<std::uint8_t> small(1024, 0);
    std::ofstream out(paths.bad_size, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(small.data()), static_cast<std::streamsize>(small.size()));
  }

  {
    D64Image image = MakeBlankImage(false);
    BuildCommonBam(image, 1, 1, "BAD DIR PTR");

    std::array<std::uint8_t, D64Reader::kSectorSize> dir{};
    dir[0] = 36;
    dir[1] = 0;
    WriteSector(image, 18, 1, dir);

    MarkUsedInBam(image, 18, 0);
    MarkUsedInBam(image, 18, 1);

    SaveImage(paths.bad_dir_pointer, image);
  }

  {
    D64Image image = MakeBlankImage(false);
    BuildCommonBam(image, 1, 1, "BAD LOOP");

    std::array<std::uint8_t, D64Reader::kSectorSize> dir{};
    dir[0] = 0;
    dir[1] = 0;
    dir[2 + 2] = 0x82;
    dir[2 + 3] = 1;
    dir[2 + 4] = 0;
    FillPetsciiName(dir, 2 + 5, "LOOP", 16);
    WriteSector(image, 18, 1, dir);

    std::array<std::uint8_t, D64Reader::kSectorSize> s{};
    s[0] = 1;
    s[1] = 0;
    s[2] = 'X';
    WriteSector(image, 1, 0, s);

    MarkUsedInBam(image, 18, 0);
    MarkUsedInBam(image, 18, 1);
    MarkUsedInBam(image, 1, 0);

    SaveImage(paths.bad_file_loop, image);
  }

  {
    D64Image image = MakeBlankImage(false);
    BuildCommonBam(image, 1, 1, "BAD NEXT");

    std::array<std::uint8_t, D64Reader::kSectorSize> dir{};
    dir[0] = 0;
    dir[1] = 0;
    dir[2 + 2] = 0x82;
    dir[2 + 3] = 1;
    dir[2 + 4] = 0;
    FillPetsciiName(dir, 2 + 5, "BADNEXT", 16);
    WriteSector(image, 18, 1, dir);

    std::array<std::uint8_t, D64Reader::kSectorSize> s{};
    s[0] = 36;
    s[1] = 0;
    s[2] = 'X';
    WriteSector(image, 1, 0, s);

    MarkUsedInBam(image, 18, 0);
    MarkUsedInBam(image, 18, 1);
    MarkUsedInBam(image, 1, 0);

    SaveImage(paths.bad_file_next_pointer, image);
  }

  return paths;
}

}  // namespace jdrive64::tests
