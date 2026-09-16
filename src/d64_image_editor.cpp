#include "jdrive64/d64_image_editor.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <set>
#include <utility>

namespace jdrive64 {

namespace {

constexpr std::uint8_t kDirStartTrack = 18;
constexpr std::uint8_t kDirStartSector = 1;
constexpr std::size_t kEntrySize = 32;
constexpr std::size_t kMaxChain = 4096;

}  // namespace

bool D64ImageEditor::Open(const std::string& image_path) {
  last_error_.clear();

  if (!reader_.Open(image_path)) {
    last_error_ = reader_.LastError();
    return false;
  }

  writer_.close();
  writer_.clear();
  writer_.open(image_path, std::ios::in | std::ios::out | std::ios::binary);
  if (!writer_) {
    last_error_ = "Cannot open D64 image for write";
    return false;
  }

  return true;
}

bool D64ImageEditor::AddFile(const std::string& host_file_path, const std::string& windows_name) {
  last_error_.clear();

  std::string base_name;
  std::string ext;
  if (!ParseWindowsName(windows_name, &base_name, &ext)) {
    last_error_ = "Invalid target name, expected NAME.EXT";
    return false;
  }

  EntryLocation existing;
  std::array<std::uint8_t, D64Reader::kSectorSize> existing_sector{};
  if (FindEntryByName(windows_name, &existing, &existing_sector)) {
    last_error_ = "File already exists";
    return false;
  }
  if (last_error_ != "File not found") {
    return false;
  }

  EntryLocation free_entry;
  std::array<std::uint8_t, D64Reader::kSectorSize> dir_sector{};
  if (!FindFreeDirectoryEntry(&free_entry, &dir_sector)) {
    return false;
  }

  std::vector<std::uint8_t> host_data;
  if (!OpenHostFile(host_file_path, &host_data)) {
    return false;
  }

  const std::size_t blocks_needed = std::max<std::size_t>(1, (host_data.size() + 253) / 254);

  std::array<std::uint8_t, D64Reader::kSectorSize> bam{};
  if (!LoadBam(&bam)) {
    return false;
  }

  std::vector<std::pair<std::uint8_t, std::uint8_t>> alloc;
  alloc.reserve(blocks_needed);

  for (std::uint8_t track = D64Reader::kMinTrack; track <= D64Reader::kMaxTrack; ++track) {
    const auto sectors = reader_.SectorsPerTrack(track);
    for (std::uint8_t sector = 0; sector < sectors; ++sector) {
      if (IsSectorFree(bam, track, sector)) {
        alloc.push_back({track, sector});
        if (alloc.size() == blocks_needed) {
          break;
        }
      }
    }
    if (alloc.size() == blocks_needed) {
      break;
    }
  }

  if (alloc.size() != blocks_needed) {
    last_error_ = "Not enough free sectors in D64 image";
    return false;
  }

  std::size_t data_offset = 0;
  for (std::size_t i = 0; i < alloc.size(); ++i) {
    std::array<std::uint8_t, D64Reader::kSectorSize> sector{};
    const auto [track, sec] = alloc[i];

    if (i + 1 < alloc.size()) {
      sector[0] = alloc[i + 1].first;
      sector[1] = alloc[i + 1].second;
      const std::size_t copy_size = std::min<std::size_t>(254, host_data.size() - data_offset);
      std::memcpy(sector.data() + 2, host_data.data() + data_offset, copy_size);
      data_offset += copy_size;
    } else {
      const std::size_t copy_size = host_data.size() - data_offset;
      sector[0] = 0;
      sector[1] = static_cast<std::uint8_t>(2 + copy_size);
      if (copy_size > 0) {
        std::memcpy(sector.data() + 2, host_data.data() + data_offset, copy_size);
      }
      data_offset += copy_size;
    }

    if (!WriteSector(track, sec, sector)) {
      return false;
    }
    MarkSectorUsed(&bam, track, sec);
  }

  const auto type = ExtensionToFileType(ext);
  dir_sector[free_entry.offset + 2] = static_cast<std::uint8_t>(0x80 | type);
  dir_sector[free_entry.offset + 3] = alloc.front().first;
  dir_sector[free_entry.offset + 4] = alloc.front().second;

  std::array<std::uint8_t, 16> name_bytes{};
  name_bytes.fill(0xA0);
  const auto norm_name = NormalizeBaseName(base_name);
  for (std::size_t i = 0; i < std::min<std::size_t>(norm_name.size(), name_bytes.size()); ++i) {
    name_bytes[i] = static_cast<std::uint8_t>(norm_name[i]);
  }
  std::memcpy(dir_sector.data() + free_entry.offset + 5, name_bytes.data(), name_bytes.size());

  dir_sector[free_entry.offset + 30] = static_cast<std::uint8_t>(blocks_needed & 0xFF);
  dir_sector[free_entry.offset + 31] = static_cast<std::uint8_t>((blocks_needed >> 8) & 0xFF);

  if (!WriteSector(free_entry.dir_track, free_entry.dir_sector, dir_sector)) {
    return false;
  }

  if (!SaveBam(bam)) {
    return false;
  }

  return true;
}

bool D64ImageEditor::DeleteFile(const std::string& windows_name) {
  last_error_.clear();

  EntryLocation loc;
  std::array<std::uint8_t, D64Reader::kSectorSize> dir_sector{};
  if (!FindEntryByName(windows_name, &loc, &dir_sector)) {
    return false;
  }

  std::vector<std::pair<std::uint8_t, std::uint8_t>> chain;
  if (!CollectFileChain(loc.start_track, loc.start_sector, &chain)) {
    return false;
  }

  std::array<std::uint8_t, D64Reader::kSectorSize> bam{};
  if (!LoadBam(&bam)) {
    return false;
  }

  for (const auto& [track, sector] : chain) {
    MarkSectorFree(&bam, track, sector);
  }

  std::array<std::uint8_t, kEntrySize> clear{};
  std::memcpy(dir_sector.data() + loc.offset, clear.data(), clear.size());

  if (!WriteSector(loc.dir_track, loc.dir_sector, dir_sector)) {
    return false;
  }

  if (!SaveBam(bam)) {
    return false;
  }

  return true;
}

bool D64ImageEditor::RenameFile(const std::string& old_windows_name, const std::string& new_windows_name) {
  last_error_.clear();

  std::string new_base;
  std::string new_ext;
  if (!ParseWindowsName(new_windows_name, &new_base, &new_ext)) {
    last_error_ = "Invalid target name, expected NAME.EXT";
    return false;
  }

  EntryLocation existing;
  std::array<std::uint8_t, D64Reader::kSectorSize> existing_sector{};
  if (ToUpper(old_windows_name) != ToUpper(new_windows_name) &&
      FindEntryByName(new_windows_name, &existing, &existing_sector)) {
    last_error_ = "Target file already exists";
    return false;
  }
  if (last_error_ != "File not found" && ToUpper(old_windows_name) != ToUpper(new_windows_name)) {
    return false;
  }

  EntryLocation loc;
  std::array<std::uint8_t, D64Reader::kSectorSize> dir_sector{};
  if (!FindEntryByName(old_windows_name, &loc, &dir_sector)) {
    return false;
  }

  std::array<std::uint8_t, 16> name_bytes{};
  name_bytes.fill(0xA0);
  const auto norm_name = NormalizeBaseName(new_base);
  for (std::size_t i = 0; i < std::min<std::size_t>(name_bytes.size(), norm_name.size()); ++i) {
    name_bytes[i] = static_cast<std::uint8_t>(norm_name[i]);
  }
  std::memcpy(dir_sector.data() + loc.offset + 5, name_bytes.data(), name_bytes.size());

  const auto new_type = ExtensionToFileType(new_ext);
  dir_sector[loc.offset + 2] = static_cast<std::uint8_t>((dir_sector[loc.offset + 2] & 0xF8) | new_type | 0x80);

  return WriteSector(loc.dir_track, loc.dir_sector, dir_sector);
}

const std::string& D64ImageEditor::LastError() const { return last_error_; }

bool D64ImageEditor::LoadBam(std::array<std::uint8_t, D64Reader::kSectorSize>* bam) {
  if (bam == nullptr) {
    last_error_ = "Invalid BAM buffer";
    return false;
  }
  return ReadSector(18, 0, bam);
}

bool D64ImageEditor::SaveBam(const std::array<std::uint8_t, D64Reader::kSectorSize>& bam) {
  return WriteSector(18, 0, bam);
}

bool D64ImageEditor::ReadSector(std::uint8_t track,
                                std::uint8_t sector,
                                std::array<std::uint8_t, D64Reader::kSectorSize>* data) {
  if (data == nullptr) {
    last_error_ = "Invalid sector buffer";
    return false;
  }
  if (!reader_.ReadSector(track, sector, data->data())) {
    last_error_ = reader_.LastError();
    return false;
  }
  return true;
}

bool D64ImageEditor::WriteSector(std::uint8_t track,
                                 std::uint8_t sector,
                                 const std::array<std::uint8_t, D64Reader::kSectorSize>& data) {
  const auto offset = reader_.TrackSectorToOffset(track, sector);
  writer_.clear();
  writer_.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!writer_) {
    last_error_ = "Cannot seek while writing sector";
    return false;
  }

  writer_.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(D64Reader::kSectorSize));
  writer_.flush();
  if (!writer_) {
    last_error_ = "Cannot write sector";
    return false;
  }

  return true;
}

bool D64ImageEditor::FindEntryByName(const std::string& windows_name,
                                     EntryLocation* location,
                                     std::array<std::uint8_t, D64Reader::kSectorSize>* sector_data) {
  std::uint8_t track = kDirStartTrack;
  std::uint8_t sector = kDirStartSector;
  std::set<std::pair<std::uint8_t, std::uint8_t>> visited;
  const auto target = ToUpper(windows_name);

  while (track != 0) {
    const auto key = std::make_pair(track, sector);
    if (!visited.insert(key).second) {
      last_error_ = "Directory loop detected";
      return false;
    }
    if (visited.size() > kMaxChain) {
      last_error_ = "Directory chain too long";
      return false;
    }

    std::array<std::uint8_t, D64Reader::kSectorSize> current{};
    if (!ReadSector(track, sector, &current)) {
      return false;
    }

    for (std::size_t offset = 2; offset + kEntrySize <= D64Reader::kSectorSize; offset += kEntrySize) {
      const auto file_type = current[offset + 2];
      if ((file_type & 0x07) == 0) {
        continue;
      }

      const auto name = DirectoryEntryToWindowsName(current, offset);
      if (ToUpper(name) == target) {
        if (location != nullptr) {
          location->dir_track = track;
          location->dir_sector = sector;
          location->offset = offset;
          location->file_type = file_type;
          location->start_track = current[offset + 3];
          location->start_sector = current[offset + 4];
        }
        if (sector_data != nullptr) {
          *sector_data = current;
        }
        return true;
      }
    }

    track = current[0];
    sector = current[1];
  }

  last_error_ = "File not found";
  return false;
}

bool D64ImageEditor::FindFreeDirectoryEntry(EntryLocation* location,
                                            std::array<std::uint8_t, D64Reader::kSectorSize>* sector_data) {
  std::uint8_t track = kDirStartTrack;
  std::uint8_t sector = kDirStartSector;
  std::set<std::pair<std::uint8_t, std::uint8_t>> visited;

  while (track != 0) {
    const auto key = std::make_pair(track, sector);
    if (!visited.insert(key).second || visited.size() > kMaxChain) {
      last_error_ = "Directory chain invalid";
      return false;
    }

    std::array<std::uint8_t, D64Reader::kSectorSize> current{};
    if (!ReadSector(track, sector, &current)) {
      return false;
    }

    for (std::size_t offset = 2; offset + kEntrySize <= D64Reader::kSectorSize; offset += kEntrySize) {
      const auto file_type = current[offset + 2];
      if ((file_type & 0x07) == 0) {
        if (location != nullptr) {
          location->dir_track = track;
          location->dir_sector = sector;
          location->offset = offset;
        }
        if (sector_data != nullptr) {
          *sector_data = current;
        }
        return true;
      }
    }

    track = current[0];
    sector = current[1];
  }

  last_error_ = "No free directory entry available";
  return false;
}

bool D64ImageEditor::ParseWindowsName(const std::string& windows_name,
                                      std::string* base_name,
                                      std::string* ext_upper) const {
  const auto pos = windows_name.find_last_of('.');
  if (pos == std::string::npos || pos == 0 || pos + 1 >= windows_name.size()) {
    return false;
  }
  if (base_name != nullptr) {
    *base_name = windows_name.substr(0, pos);
  }
  if (ext_upper != nullptr) {
    *ext_upper = ToUpper(windows_name.substr(pos + 1));
  }
  return true;
}

std::uint8_t D64ImageEditor::ExtensionToFileType(const std::string& ext_upper) const {
  if (ext_upper == "DEL") {
    return 0;
  }
  if (ext_upper == "SEQ") {
    return 1;
  }
  if (ext_upper == "PRG") {
    return 2;
  }
  if (ext_upper == "USR") {
    return 3;
  }
  if (ext_upper == "REL") {
    return 4;
  }
  return 2;
}

std::string D64ImageEditor::FileTypeToExtension(std::uint8_t file_type) const {
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
      return "PRG";
  }
}

std::string D64ImageEditor::NormalizeBaseName(const std::string& value) const {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc) != 0 || c == '_' || c == '-' || c == '+' || c == '.') {
      out.push_back(static_cast<char>(std::toupper(uc)));
    } else {
      out.push_back('_');
    }
  }
  if (out.empty()) {
    out = "UNNAMED";
  }
  return out;
}

std::string D64ImageEditor::ToUpper(std::string value) const {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return value;
}

std::string D64ImageEditor::DirectoryEntryToWindowsName(
    const std::array<std::uint8_t, D64Reader::kSectorSize>& sector,
    std::size_t offset) const {
  std::string name;
  name.reserve(16);
  for (std::size_t i = 0; i < 16; ++i) {
    const auto ch = sector[offset + 5 + i];
    if (ch == 0xA0) {
      name.push_back(' ');
    } else if (ch >= 0x20 && ch <= 0x7E) {
      name.push_back(static_cast<char>(ch));
    } else {
      name.push_back('_');
    }
  }
  while (!name.empty() && name.back() == ' ') {
    name.pop_back();
  }

  return NormalizeBaseName(name) + "." + FileTypeToExtension(sector[offset + 2]);
}

bool D64ImageEditor::IsSectorFree(const std::array<std::uint8_t, D64Reader::kSectorSize>& bam,
                                  std::uint8_t track,
                                  std::uint8_t sector) const {
  if (track < D64Reader::kMinTrack || track > D64Reader::kMaxTrack ||
      sector >= reader_.SectorsPerTrack(track)) {
    return false;
  }
  const std::size_t entry = 4 + static_cast<std::size_t>(track - 1) * 4;
  const std::size_t byte_index = 1 + static_cast<std::size_t>(sector / 8);
  const std::uint8_t bit = static_cast<std::uint8_t>(1U << (sector % 8));
  return (bam[entry + byte_index] & bit) != 0;
}

void D64ImageEditor::MarkSectorUsed(std::array<std::uint8_t, D64Reader::kSectorSize>* bam,
                                    std::uint8_t track,
                                    std::uint8_t sector) {
  const std::size_t entry = 4 + static_cast<std::size_t>(track - 1) * 4;
  const std::size_t byte_index = 1 + static_cast<std::size_t>(sector / 8);
  const std::uint8_t bit = static_cast<std::uint8_t>(1U << (sector % 8));
  if (((*bam)[entry + byte_index] & bit) != 0) {
    (*bam)[entry + byte_index] = static_cast<std::uint8_t>((*bam)[entry + byte_index] & ~bit);
    if ((*bam)[entry] > 0) {
      --(*bam)[entry];
    }
  }
}

void D64ImageEditor::MarkSectorFree(std::array<std::uint8_t, D64Reader::kSectorSize>* bam,
                                    std::uint8_t track,
                                    std::uint8_t sector) {
  const std::size_t entry = 4 + static_cast<std::size_t>(track - 1) * 4;
  const std::size_t byte_index = 1 + static_cast<std::size_t>(sector / 8);
  const std::uint8_t bit = static_cast<std::uint8_t>(1U << (sector % 8));
  if (((*bam)[entry + byte_index] & bit) == 0) {
    (*bam)[entry + byte_index] = static_cast<std::uint8_t>((*bam)[entry + byte_index] | bit);
    ++(*bam)[entry];
  }
}

bool D64ImageEditor::CollectFileChain(
    std::uint8_t start_track,
    std::uint8_t start_sector,
    std::vector<std::pair<std::uint8_t, std::uint8_t>>* chain) {
  if (chain == nullptr) {
    last_error_ = "Invalid chain output";
    return false;
  }
  chain->clear();

  if (start_track == 0) {
    return true;
  }

  std::set<std::pair<std::uint8_t, std::uint8_t>> visited;
  std::uint8_t track = start_track;
  std::uint8_t sector = start_sector;

  while (track != 0) {
    if (track < D64Reader::kMinTrack || track > D64Reader::kMaxTrack ||
        sector >= reader_.SectorsPerTrack(track)) {
      last_error_ = "Invalid file chain pointer";
      return false;
    }

    const auto key = std::make_pair(track, sector);
    if (!visited.insert(key).second) {
      last_error_ = "File chain loop detected";
      return false;
    }
    if (visited.size() > kMaxChain) {
      last_error_ = "File chain too long";
      return false;
    }

    chain->push_back(key);

    std::array<std::uint8_t, D64Reader::kSectorSize> data{};
    if (!ReadSector(track, sector, &data)) {
      return false;
    }

    track = data[0];
    sector = data[1];
  }

  return true;
}

bool D64ImageEditor::OpenHostFile(const std::string& host_file_path, std::vector<std::uint8_t>* data) {
  if (data == nullptr) {
    last_error_ = "Invalid host file output";
    return false;
  }
  std::ifstream in(host_file_path, std::ios::binary);
  if (!in) {
    last_error_ = "Cannot open host file";
    return false;
  }

  in.seekg(0, std::ios::end);
  const auto end_pos = in.tellg();
  if (end_pos < 0) {
    last_error_ = "Cannot read host file size";
    return false;
  }
  in.seekg(0, std::ios::beg);

  data->assign(static_cast<std::size_t>(end_pos), 0);
  if (!data->empty()) {
    in.read(reinterpret_cast<char*>(data->data()), static_cast<std::streamsize>(data->size()));
    if (!in) {
      last_error_ = "Cannot read host file";
      return false;
    }
  }

  return true;
}

}  // namespace jdrive64
