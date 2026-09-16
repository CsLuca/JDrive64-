#include "jdrive64/petscii_converter.hpp"

#include <cctype>

namespace {

char MapPetscii(std::uint8_t ch) {
  if (ch == 0xA0 || ch == 0x20) {
    return ' ';
  }

  if (ch >= 0x41 && ch <= 0x5A) {
    return static_cast<char>(ch);
  }
  if (ch >= 0xC1 && ch <= 0xDA) {
    return static_cast<char>('A' + (ch - 0xC1));
  }
  if (ch >= 'a' && ch <= 'z') {
    return static_cast<char>(std::toupper(ch));
  }

  if (ch >= 0x30 && ch <= 0x39) {
    return static_cast<char>(ch);
  }

  switch (ch) {
    case 0x2D:
      return '-';
    case 0x2E:
      return '.';
    case 0x5F:
      return '_';
    case 0x2B:
      return '+';
    case 0x2C:
      return ',';
    case 0x21:
      return '!';
    case 0x3F:
      return '?';
    default:
      return '_';
  }
}

}  // namespace

namespace jdrive64 {

std::string PetsciiToUtf8(const std::uint8_t* source, int size) {
  std::string out;
  if (source == nullptr || size <= 0) {
    return out;
  }

  out.reserve(static_cast<std::size_t>(size));

  for (int i = 0; i < size; ++i) {
    out.push_back(MapPetscii(source[i]));
  }

  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }

  return out;
}

}  // namespace jdrive64
