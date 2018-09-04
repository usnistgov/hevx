#ifndef HEV_IRIS_DSO_DESKTOP_WINDOW_H_
#define HEV_IRIS_DSO_DESKTOP_WINDOW_H_

#include "dso.h"

namespace iris {

class DesktopWindow : public DSO {
public:
  DesktopWindow();

  virtual std::error_code
  Control(std::string_view,
          std::vector<std::string_view> const&) noexcept override;

  virtual ~DesktopWindow() noexcept;
}; // class DesktopWindow

} // namespace iris

#endif // HEV_IRIS_DSO_DESKTOP_WINDOW_H_
