#ifndef HEV_IRIS_RENDERER_GLCONTEXT_H_
#define HEV_IRIS_RENDERER_GLCONTEXT_H_

#include "iris/wsi/window.h"
#include <memory>

namespace iris::Renderer {

class GLContext {
public:
  static tl::expected<GLContext, std::error_code>
  Create(wsi::Window& window) noexcept;

  void MakeCurrent() noexcept;

  //! \brief Default constructor: no initialization.
  GLContext() noexcept;
  //! \brief No copies.
  GLContext(GLContext const&) = delete;
  //! \brief Move constructor.
  GLContext(GLContext&&) noexcept;
  //! \brief No copies.
  GLContext& operator=(GLContext const&) = delete;
  //! \brief Move assignment operator.
  GLContext& operator=(GLContext&&) noexcept;
  //! \brief Destructor.
  ~GLContext() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> pImpl_;
}; // class GLContext

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_GLCONTEXT_H_
