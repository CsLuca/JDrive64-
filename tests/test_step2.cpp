#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "d64_test_utils.hpp"

#include "jdrive64/bam_reader.hpp"
#include "jdrive64/d64_reader.hpp"
#include "jdrive64/disk_image_session.hpp"
#include "jdrive64/directory_reader.hpp"
#include "jdrive64/file_chain_reader.hpp"

namespace {

using jdrive64::BAMReader;
using jdrive64::D64Reader;
using jdrive64::DiskImageSession;
using jdrive64::DirectoryReader;
using jdrive64::FileChainReader;
using jdrive64::tests::CreateGoldenCorpus;
using jdrive64::tests::GoldenPaths;

bool Check(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    return false;
  }
  return true;
}

bool TestTrackSectorMath() {
  D64Reader reader;
  bool ok = true;
  ok = ok && Check(reader.SectorsPerTrack(1) == 21, "track 1 sectors");
  ok = ok && Check(reader.SectorsPerTrack(18) == 19, "track 18 sectors");
  ok = ok && Check(reader.SectorsPerTrack(25) == 18, "track 25 sectors");
  ok = ok && Check(reader.SectorsPerTrack(31) == 17, "track 31 sectors");
  ok = ok && Check(reader.TrackSectorToOffset(1, 0) == 0, "offset t1/s0");
  ok = ok && Check(reader.TrackSectorToOffset(18, 0) == 357U * 256U, "offset t18/s0");
  return ok;
}

bool TestValidSmall(const GoldenPaths& p) {
  D64Reader r;
  if (!Check(r.Open(p.valid_small.string()), "open valid_small")) {
    return false;
  }

  BAMReader bam;
  if (!Check(bam.Load(r), "bam load valid_small")) {
    return false;
  }
  if (!Check(bam.FreeBlocks() == 30, "bam free blocks valid_small")) {
    return false;
  }

  DirectoryReader dir;
  if (!Check(dir.Load(r), "directory load valid_small")) {
    return false;
  }
  if (!Check(dir.Entries().size() == 1, "directory count valid_small")) {
    return false;
  }

  FileChainReader chain(r);
  auto bytes = chain.ReadFile(dir.Entries()[0]);
  if (!Check(chain.LastError().empty(), "file chain valid_small no error")) {
    return false;
  }
  const std::string s(bytes.begin(), bytes.end());
  return Check(s == "HELLO", "file chain valid_small payload HELLO");
}

bool TestValidMulti(const GoldenPaths& p) {
  D64Reader r;
  if (!Check(r.Open(p.valid_multi.string()), "open valid_multi")) {
    return false;
  }

  DirectoryReader dir;
  if (!Check(dir.Load(r), "directory load valid_multi")) {
    return false;
  }
  if (!Check(dir.Entries().size() == 2, "directory count valid_multi")) {
    return false;
  }

  FileChainReader chain(r);
  auto b0 = chain.ReadFile(dir.Entries()[0]);
  if (!Check(chain.LastError().empty(), "file0 valid_multi no error")) {
    return false;
  }
  auto b1 = chain.ReadFile(dir.Entries()[1]);
  if (!Check(chain.LastError().empty(), "file1 valid_multi no error")) {
    return false;
  }

  const std::string s0(b0.begin(), b0.end());
  const std::string s1(b1.begin(), b1.end());
  return Check(s0 == "ONE" && s1 == "DATA", "valid_multi payloads");
}

bool TestValidErrorInfo(const GoldenPaths& p) {
  D64Reader r;
  if (!Check(r.Open(p.valid_errorinfo.string()), "open valid_errorinfo")) {
    return false;
  }
  DirectoryReader dir;
  if (!Check(dir.Load(r), "directory load valid_errorinfo")) {
    return false;
  }
  return Check(dir.Entries().empty(), "valid_errorinfo empty directory");
}

bool TestCorrupted(const GoldenPaths& p) {
  bool ok = true;

  D64Reader bad_size;
  ok = ok && Check(!bad_size.Open(p.bad_size.string()), "bad_size must fail open");

  D64Reader bad_dir;
  if (!Check(bad_dir.Open(p.bad_dir_pointer.string()), "open bad_dir_pointer")) {
    return false;
  }
  DirectoryReader dir;
  ok = ok && Check(!dir.Load(bad_dir), "bad_dir_pointer must fail directory load");

  D64Reader bad_loop;
  if (!Check(bad_loop.Open(p.bad_file_loop.string()), "open bad_file_loop")) {
    return false;
  }
  DirectoryReader dir_loop;
  if (!Check(dir_loop.Load(bad_loop), "load dir bad_file_loop")) {
    return false;
  }
  FileChainReader chain_loop(bad_loop);
  (void)chain_loop.ReadFile(dir_loop.Entries()[0]);
  ok = ok && Check(!chain_loop.LastError().empty(), "bad_file_loop chain must fail");

  D64Reader bad_next;
  if (!Check(bad_next.Open(p.bad_file_next_pointer.string()), "open bad_file_next_pointer")) {
    return false;
  }
  DirectoryReader dir_next;
  if (!Check(dir_next.Load(bad_next), "load dir bad_file_next_pointer")) {
    return false;
  }
  FileChainReader chain_next(bad_next);
  (void)chain_next.ReadFile(dir_next.Entries()[0]);
  ok = ok && Check(!chain_next.LastError().empty(), "bad_file_next_pointer chain must fail");

  return ok;
}

bool TestSharedDomainSession(const GoldenPaths& p) {
  DiskImageSession session;
  if (!Check(session.Open(p.valid_multi.string()), "session open valid_multi")) {
    return false;
  }

  if (!Check(session.Catalog().Files().size() == 2, "session catalog file count")) {
    return false;
  }

  std::vector<std::uint8_t> one;
  std::vector<std::uint8_t> data;
  if (!Check(session.ReadFileByWindowsName("ONE.PRG", &one), "session read ONE.PRG")) {
    return false;
  }
  if (!Check(session.ReadFileByWindowsName("DATA.SEQ", &data), "session read DATA.SEQ")) {
    return false;
  }

  const std::string s0(one.begin(), one.end());
  const std::string s1(data.begin(), data.end());
  if (!Check(s0 == "ONE" && s1 == "DATA", "session read payloads")) {
    return false;
  }

  std::vector<std::uint8_t> missing;
  if (!Check(!session.ReadFileByWindowsName("MISSING.PRG", &missing), "session missing file fails")) {
    return false;
  }
  return Check(session.LastError() == "File not found", "session missing file error");
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "jdrive64_step2_golden";
  const auto golden = CreateGoldenCorpus(root);

  bool ok = true;
  ok = ok && TestTrackSectorMath();
  ok = ok && TestValidSmall(golden);
  ok = ok && TestValidMulti(golden);
  ok = ok && TestValidErrorInfo(golden);
  ok = ok && TestCorrupted(golden);
  ok = ok && TestSharedDomainSession(golden);

  if (!ok) {
    return 1;
  }

  std::cout << "Step2 core+golden tests passed\n";
  return 0;
}
