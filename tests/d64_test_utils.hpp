#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "jdrive64/d64_reader.hpp"

namespace jdrive64::tests {

struct D64Image {
  std::vector<std::uint8_t> bytes;
};

struct GoldenPaths {
  std::filesystem::path root;
  std::filesystem::path valid_small;
  std::filesystem::path valid_multi;
  std::filesystem::path valid_errorinfo;
  std::filesystem::path bad_size;
  std::filesystem::path bad_dir_pointer;
  std::filesystem::path bad_file_loop;
  std::filesystem::path bad_file_next_pointer;
};

std::size_t TrackSectorToOffset(std::uint8_t track, std::uint8_t sector);

void WriteSector(D64Image& image,
                 std::uint8_t track,
                 std::uint8_t sector,
                 const std::array<std::uint8_t, D64Reader::kSectorSize>& sector_data);

D64Image MakeBlankImage(bool with_error_info = false);
GoldenPaths CreateGoldenCorpus(const std::filesystem::path& root);

}  // namespace jdrive64::tests
