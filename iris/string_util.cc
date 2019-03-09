#include "string_util.h"

#include "iris/config.h"

#include <stdexcept>
#include <vector>

#if PLATFORM_WINDOWS

#include <Windows.h>

std::wstring iris::string_to_wstring(std::string const& str) {
  if (str.empty()) return {};

  int const size = ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
  if (size == 0) return {};

  std::vector<wchar_t> bytes(size);
  int const count = ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1,
                                          bytes.data(), bytes.size());
  if (count == 0) throw std::runtime_error("string_to_wstring");

  return bytes.data();
} // string_to_wstring

std::string iris::wstring_to_string(std::wstring const& wstr) {
  if (wstr.empty()) return {};

  int const size =
    ::WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
  if (size == 0) return {};

  std::vector<char> bytes(size);
  int const count = ::WideCharToMultiByte(
    CP_UTF8, 0, wstr.c_str(), -1, bytes.data(), bytes.size(), NULL, NULL);
  if (count == 0) throw std::runtime_error("wstring_to_string");

  return bytes.data();
} // wstring_to_string

#endif
