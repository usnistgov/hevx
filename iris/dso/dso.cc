#include "dso/dso.h"
#include "dso/desktop_window.h"
#include <functional>
#include <string>
#include <unordered_map>

namespace iris {

static std::unordered_map<std::string, std::function<DSO*()>> sMap{
  {"DesktopWindow", []() { return new DesktopWindow; }}};

} // namespace iris

std::unique_ptr<iris::DSO> iris::DSO::Instantiate(std::string_view name) {
  if (auto iter = sMap.find(std::string(name)); iter != sMap.end()) {
    return std::unique_ptr<DSO>(iter->second());
  } else {
    return nullptr;
  }
} // iris::DSO::Instantiate
