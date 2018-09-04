#ifndef HEV_IRIS_DSO_DSO_H_
#define HEV_IRIS_DSO_DSO_H_

#include <string_view>
#include <system_error>
#include <vector>

namespace iris {

class DSO {
public:
  DSO() noexcept = default;
  virtual ~DSO() noexcept {}

  virtual std::error_code
  Control(std::string_view command,
          std::vector<std::string_view> const& components) noexcept = 0;

  static std::unique_ptr<DSO> Instantiate(std::string_view name);
}; // class DSO

} // namespace iris

#endif // HEV_IRIS_DSO_DSO_H_
