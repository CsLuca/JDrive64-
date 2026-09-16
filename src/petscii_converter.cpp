#include "jdrive64/petscii_converter.hpp"

#include <cctype>

namespace jdrive64 {

std::string PetsciiToUtf8(const std::uint8_t* source, int size) {
  std::string out;
  if (source == nullptr || size <= 0) {
    return out;
  }

  out.reserve(static_cast<std::size_t>(size));

  for (int i = 0; i < size; ++i) {
    const std::uint8_t ch = source[i];
    if (ch == 0xA0) {
      out.push_back(' ');
    } else if (ch >= 'a' && ch <= 'z') {
      out.push_back(static_cast<char>(std::toupper(ch)));
    } else if (ch >= 0x20 && ch <= 0x7E) {
      out.push_back(static_cast<char>(ch));
    } else {
      out.push_back('_');
    }
  }

  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }

  return out;
}

}  // namespace jdrive64
