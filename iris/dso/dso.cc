#include "dso/dso.h"
#include "dso/desktop_window.h"
#include <functional>
#include <string>
#include <unordered_map>

std::unique_ptr<iris::DSO> iris::DSO::Instantiate(std::string_view name) {
  static std::unordered_map<std::string, std::function<DSO*()>> sMap{
    {"desktopWindow", []() { return new DesktopWindow; }}};

  if (auto iter = sMap.find(std::string(name)); iter != sMap.end()) {
    return std::unique_ptr<DSO>(iter->second());
  } else {
    return nullptr;
  }
} // iris::DSO::Instantiate
