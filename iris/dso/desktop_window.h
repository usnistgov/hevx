#ifndef HEV_IRIS_DSO_DESKTOP_WINDOW_H_
#define HEV_IRIS_DSO_DESKTOP_WINDOW_H_

#include "iris/dso/dso.h"
#include "iris/wsi/window.h"
#include "iris/renderer/surface.h"

namespace iris {

class DesktopWindow : public DSO {
public:
  DesktopWindow() = default;

  virtual std::error_code Initialize() noexcept override;

  virtual std::error_code Frame() noexcept override;

  virtual std::error_code
  Control(std::string_view,
          std::vector<std::string_view> const&) noexcept override;

  virtual ~DesktopWindow() noexcept = default;

private:
  bool sResized_{false};

  wsi::Window window_;
  Renderer::Surface surface_;
}; // class DesktopWindow

} // namespace iris

#endif // HEV_IRIS_DSO_DESKTOP_WINDOW_H_
