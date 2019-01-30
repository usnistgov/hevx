#include "renderer/window.h"
#include "absl/strings/str_split.h"
#include "error.h"
#include "glm/gtc/matrix_access.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "logging.h"
#include "renderer/impl.h"
#include "renderer/renderer.h"

tl::expected<iris::Renderer::Window, std::exception>
iris::Renderer::Window::Create(gsl::czstring<> title, wsi::Offset2D offset,
                               wsi::Extent2D extent,
                               glm::vec4 const& clearColor,
                               Options const& options, int display) noexcept {
  IRIS_LOG_ENTER();

  wsi::Window::Options windowOptions = wsi::Window::Options::kSizeable;
  if ((options & Window::Options::kDecorated) == Window::Options::kDecorated) {
    windowOptions |= wsi::Window::Options::kDecorated;
  }

  Window window;
  if (auto win = wsi::Window::Create(
        title, std::move(offset), std::move(extent), windowOptions, display)) {
    window.window = std::move(*win);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(win.error());
  }

  if (auto sfc = Surface::Create(window.window, clearColor)) {
    window.surface = std::move(*sfc);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(sfc.error());
  }

  window.showUI =
    (options & Window::Options::kShowUI) == Window::Options::kShowUI;

  window.uiContext.reset(ImGui::CreateContext());
  ImGui::SetCurrentContext(window.uiContext.get());
  ImGui::StyleColorsDark();

  ImGuiIO& io = ImGui::GetIO();

  io.KeyMap[ImGuiKey_Tab] = static_cast<int>(wsi::Keys::kTab);
  io.KeyMap[ImGuiKey_LeftArrow] = static_cast<int>(wsi::Keys::kLeft);
  io.KeyMap[ImGuiKey_RightArrow] = static_cast<int>(wsi::Keys::kRight);
  io.KeyMap[ImGuiKey_UpArrow] = static_cast<int>(wsi::Keys::kUp);
  io.KeyMap[ImGuiKey_DownArrow] = static_cast<int>(wsi::Keys::kDown);
  io.KeyMap[ImGuiKey_PageUp] = static_cast<int>(wsi::Keys::kPageUp);
  io.KeyMap[ImGuiKey_PageDown] = static_cast<int>(wsi::Keys::kPageDown);
  io.KeyMap[ImGuiKey_Home] = static_cast<int>(wsi::Keys::kHome);
  io.KeyMap[ImGuiKey_End] = static_cast<int>(wsi::Keys::kEnd);
  io.KeyMap[ImGuiKey_Insert] = static_cast<int>(wsi::Keys::kInsert);
  io.KeyMap[ImGuiKey_Delete] = static_cast<int>(wsi::Keys::kDelete);
  io.KeyMap[ImGuiKey_Backspace] = static_cast<int>(wsi::Keys::kBackspace);
  io.KeyMap[ImGuiKey_Space] = static_cast<int>(wsi::Keys::kSpace);
  io.KeyMap[ImGuiKey_Enter] = static_cast<int>(wsi::Keys::kEnter);
  io.KeyMap[ImGuiKey_Escape] = static_cast<int>(wsi::Keys::kEscape);
  io.KeyMap[ImGuiKey_A] = static_cast<int>(wsi::Keys::kA);
  io.KeyMap[ImGuiKey_C] = static_cast<int>(wsi::Keys::kC);
  io.KeyMap[ImGuiKey_V] = static_cast<int>(wsi::Keys::kV);
  io.KeyMap[ImGuiKey_X] = static_cast<int>(wsi::Keys::kX);
  io.KeyMap[ImGuiKey_Y] = static_cast<int>(wsi::Keys::kY);
  io.KeyMap[ImGuiKey_Z] = static_cast<int>(wsi::Keys::kZ);

  if (window.showUI) {
    if (auto ui = UI::Create()) {
      window.ui = std::move(*ui);
    } else {
      IRIS_LOG_LEAVE();
      return tl::unexpected(ui.error());
    }

  }

  window.projectionMatrix = glm::perspectiveFov(
    glm::radians(60.f), static_cast<float>(window.window.Extent().width),
    static_cast<float>(window.window.Extent().height), 0.1f, 1000.f);
  window.projectionMatrix[1][1] *= -1;
  window.projectionMatrixInverse = glm::inverse(window.projectionMatrix);

  window.window.Show();

  window.window.OnResize(
    std::bind(&Window::Resize, &window, std::placeholders::_1));
  window.window.OnClose(std::bind(&Window::Close, &window));

  IRIS_LOG_LEAVE();
  return std::move(window);
} // iris::Renderer::Window::Create

void iris::Renderer::Window::Resize(wsi::Extent2D const& newExtent) noexcept {
  GetLogger()->debug("Window resized: ({}x{})", newExtent.width,
                     newExtent.height);

  projectionMatrix =
    glm::perspectiveFov(glm::radians(60.f), static_cast<float>(newExtent.width),
                        static_cast<float>(newExtent.height), 0.1f, 1000.f);
  projectionMatrixInverse = glm::inverse(projectionMatrix);

  resized = true;
} // iris::Renderer::Window::Resize

void iris::Renderer::Window::Close() noexcept {
  GetLogger()->debug("Window closing");
  Renderer::Terminate();
} // iris::Renderer::Window::Close

std::system_error
iris::Renderer::Window::BeginFrame(float frameDelta[[maybe_unused]]) noexcept {
  ImGui::SetCurrentContext(uiContext.get());
  window.PollEvents();

  if (ImGui::IsKeyReleased(wsi::Keys::kEscape)) Terminate();

  if (resized) {
    auto const extent = window.Extent();
    if (auto error = surface.Resize({extent.width, extent.height});
        error.code()) {
      return error;
    }
    resized = false;
  }

  ImGuiIO& io = ImGui::GetIO();
  io.DeltaTime = frameDelta;

  io.KeyCtrl = io.KeysDown[wsi::Keys::kRightControl] |
               io.KeysDown[wsi::Keys::kLeftControl];
  io.KeyShift =
    io.KeysDown[wsi::Keys::kLeftShift] | io.KeysDown[wsi::Keys::kRightShift];
  io.KeyAlt =
    io.KeysDown[wsi::Keys::kLeftAlt] | io.KeysDown[wsi::Keys::kRightAlt];
  io.KeySuper =
    io.KeysDown[wsi::Keys::kLeftSuper] | io.KeysDown[wsi::Keys::kRightSuper];

  auto const cursorPos = window.CursorPos();
  io.MousePos = ImVec2(cursorPos.x, cursorPos.y);

  io.DisplaySize.x = static_cast<float>(window.Extent().width);
  io.DisplaySize.y = static_cast<float>(window.Extent().height);
  io.DisplayFramebufferScale = {0.f, 0.f};

  ImGui::NewFrame();

  if (showUI) {
    if (auto error = ui.BeginFrame(frameDelta); error.code()) {
      GetLogger()->error("Error beginning ui frame: {}", error.what());
      return error;
    }
  }

  return {Error::kNone};
} // iris::Renderer::Window::BeginFrame

tl::expected<VkCommandBuffer, std::system_error>
iris::Renderer::Window::EndFrame(VkFramebuffer framebuffer) noexcept {
  Expects(framebuffer != VK_NULL_HANDLE);

  ImGui::SetCurrentContext(uiContext.get());
  ImGui::EndFrame();

  if (showUI) {
    return ui.EndFrame(framebuffer);
  } else {
    return VkCommandBuffer(VK_NULL_HANDLE);
  }
} // iris::Renderer::Window::EndFrame

iris::Renderer::Window::Window(Window&& other) noexcept
  : resized(other.resized)
  , window(std::move(other.window))
  , surface(std::move(other.surface))
  , showUI(other.showUI)
  , uiContext(std::move(other.uiContext))
  , ui(std::move(other.ui))
  , projectionMatrix(std::move(other.projectionMatrix)) {
  // Re-bind delegates
  window.OnResize(std::bind(&Window::Resize, this, std::placeholders::_1));
  window.OnClose(std::bind(&Window::Close, this));
} // iris::Renderer::Window::Window

iris::Renderer::Window& iris::Renderer::Window::
operator=(Window&& rhs) noexcept {
  if (this == &rhs) return *this;

  resized = rhs.resized;
  window = std::move(rhs.window);
  surface = std::move(rhs.surface);
  showUI = rhs.showUI;
  uiContext = std::move(rhs.uiContext);
  ui = std::move(rhs.ui);
  projectionMatrix = std::move(rhs.projectionMatrix);

  // Re-bind delegates
  window.OnResize(std::bind(&Window::Resize, this, std::placeholders::_1));
  window.OnClose(std::bind(&Window::Close, this));

  return *this;
} // iris::Renderer::Window::operator=

