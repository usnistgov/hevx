#ifndef HEV_IRIS_STRING_UTIL_H_
#define HEV_IRIS_STRING_UTIL_H_

#include <string>

namespace iris {

std::wstring string_to_wstring(std::string const& str);
std::string wstring_to_string(std::wstring const& wstr);

} // namespace iris

#endif // HEV_IRIS_STRING_UTIL_H_
