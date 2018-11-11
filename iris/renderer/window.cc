#include "renderer/window.h"
#include "absl/strings/str_split.h"
#include "error.h"
#include "glm/gtc/type_ptr.hpp"
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

  if (auto ui = UI::Create()) {
    window.ui = std::move(*ui);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(ui.error());
  }

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
  resized = true;
} // iris::Renderer::Window::Resize

void iris::Renderer::Window::Close() noexcept {
  GetLogger()->debug("Window closing");
  Renderer::Terminate();
} // iris::Renderer::Window::Close

std::system_error iris::Renderer::Window::BeginFrame() noexcept {
  window.PollEvents();

  if (resized) {
    auto const extent = window.Extent();
    if (auto error = surface.Resize({extent.width, extent.height});
        error.code()) {
      return error;
    }
    resized = false;
  }

  ImGui::SetCurrentContext(ui.context.get());
  ImGuiIO& io = ImGui::GetIO();

  wsi::Keyset const keyState = window.KeyboardState();
  for (std::size_t i = 0; i <  wsi::Keyset::kMaxKeys; ++i) {
    io.KeysDown[i] = keyState[static_cast<wsi::Keys>(i)];
  }

  io.KeyCtrl = io.KeysDown[wsi::Keys::kLeftControl] ||
               io.KeysDown[wsi::Keys::kRightControl];
  io.KeyShift =
    io.KeysDown[wsi::Keys::kLeftShift] || io.KeysDown[wsi::Keys::kRightShift];
  io.KeyAlt =
    io.KeysDown[wsi::Keys::kLeftAlt] || io.KeysDown[wsi::Keys::kRightAlt];

  wsi::Buttonset const buttonState = window.ButtonState();
  for (std::size_t i = 0; i < wsi::Buttonset::kMaxButtons; ++i) {
    io.MouseDown[i] = buttonState[static_cast<wsi::Buttons>(i)];
  }

  auto const mousePos = window.CursorPos();
  io.MousePos.x = static_cast<float>(mousePos.x);
  io.MousePos.y = static_cast<float>(mousePos.y);

  UI::TimePoint currentTime = std::chrono::steady_clock::now();
  io.DeltaTime = ui.previousTime.time_since_epoch().count() > 0
                   ? (currentTime - ui.previousTime).count()
                   : 1.f / 60.f;
  ui.previousTime = currentTime;

  io.DisplaySize.x = static_cast<float>(window.Extent().width);
  io.DisplaySize.y = static_cast<float>(window.Extent().height);
  io.DisplayFramebufferScale = {0.f, 0.f};

  ImGui::NewFrame();
  return {Error::kNone};
} // iris::Renderer::Window::BeginFrame

tl::expected<VkCommandBuffer, std::system_error>
iris::Renderer::Window::EndFrame(VkFramebuffer framebuffer) noexcept {
  Expects(framebuffer != VK_NULL_HANDLE);

  ImGui::SetCurrentContext(ui.context.get());
  ImGui::EndFrame();
  ImGui::Render();

  ImDrawData* drawData = ImGui::GetDrawData();
  if (drawData->TotalVtxCount == 0) return VkCommandBuffer{VK_NULL_HANDLE};

  VkDeviceSize newBufferSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
  if (newBufferSize > ui.vertexBuffer.size) {
    if (auto vb =
          Buffer::Create(newBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VMA_MEMORY_USAGE_CPU_TO_GPU, "ui::vertexBuffer")) {
      auto newVB = std::move(*vb);
      // ensures old ui.vertexBuffer will get destroyed on scope exit
      std::swap(ui.vertexBuffer, newVB);
    } else {
      using namespace std::string_literals;
      return tl::unexpected(std::system_error(
        vb.error().code(),
        "Cannot resize UI vertex buffer: "s + vb.error().what()));
    }
  }

  newBufferSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);
  if (newBufferSize > ui.indexBuffer.size) {
    if (auto ib =
          Buffer::Create(newBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         VMA_MEMORY_USAGE_CPU_TO_GPU, "ui::sIndexBuffer")) {
      auto newIB = std::move(*ib);
      // ensures old ui.indexBuffer will get destroyed on scope exit
      std::swap(ui.indexBuffer, newIB);
    } else {
      using namespace std::string_literals;
      return tl::unexpected(std::system_error(
        ib.error().code(),
        "Cannot resize UI index buffer: "s + ib.error().what()));
    }
  }

  ImDrawVert* pVerts;
  if (auto p = ui.vertexBuffer.Map<ImDrawVert*>()) {
    pVerts = *p;
  } else {
    using namespace std::string_literals;
    return tl::unexpected(std::system_error(
      p.error().code(),
      "Cannot map UI vertex staging buffer: "s + p.error().what()));
  }

  ImDrawIdx* pIndxs;
  if (auto p = ui.indexBuffer.Map<ImDrawIdx*>()) {
    pIndxs = *p;
  } else {
    using namespace std::string_literals;
    return tl::unexpected(std::system_error(
      p.error().code(),
      "Cannot map UI index staging buffer: "s + p.error().what()));
  }

  for (int i = 0; i < drawData->CmdListsCount; ++i) {
    ImDrawList const* cmdList = drawData->CmdLists[i];
    std::memcpy(pVerts, cmdList->VtxBuffer.Data,
                cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
    std::memcpy(pIndxs, cmdList->IdxBuffer.Data,
                cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
    pVerts += cmdList->VtxBuffer.Size;
    pIndxs += cmdList->IdxBuffer.Size;
  }

  ui.vertexBuffer.Unmap();
  ui.indexBuffer.Unmap();

  absl::FixedArray<VkClearValue> clearValues(4);
  clearValues[sColorTargetAttachmentIndex].color = {{0, 0, 0, 0}};
  clearValues[sDepthStencilTargetAttachmentIndex].depthStencil = {1.f, 0};

  GetLogger()->flush();
  ui.commandBufferIndex =
    (ui.commandBufferIndex + 1) % ui.commandBuffers.size();
  auto&& cb = ui.commandBuffers[ui.commandBufferIndex];

  VkCommandBufferInheritanceInfo inheritanceInfo = {};
  inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
  inheritanceInfo.renderPass = sRenderPass;
  inheritanceInfo.framebuffer = framebuffer;

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT |
                    VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
  beginInfo.pInheritanceInfo = &inheritanceInfo;

  if (auto result = vkBeginCommandBuffer(cb, &beginInfo);
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot begin UI command buffer"));
  }

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ui.pipeline);
  vkCmdBindDescriptorSets(
    cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ui.pipeline.layout, 0,
    gsl::narrow_cast<std::uint32_t>(ui.descriptorSets.sets.size()),
    ui.descriptorSets.sets.data(), 0, nullptr);

  VkDeviceSize bindingOffset = 0;
  vkCmdBindVertexBuffers(cb, 0, 1, ui.vertexBuffer.get(), &bindingOffset);
  vkCmdBindIndexBuffer(cb, ui.indexBuffer, 0, VK_INDEX_TYPE_UINT16);

  glm::vec2 const displaySize{drawData->DisplaySize.x, drawData->DisplaySize.y};
  glm::vec2 const displayPos{drawData->DisplayPos.x, drawData->DisplayPos.y};

  VkViewport viewport = {0, 0, displaySize.x, displaySize.y, 0.f, 1.f};
  vkCmdSetViewport(cb, 0, 1, &viewport);

  glm::vec2 const scale = glm::vec2{2.f, 2.f} / displaySize;
  glm::vec2 const translate = glm::vec2{-1.f, -1.f} - displayPos * scale;

  vkCmdPushConstants(cb, ui.pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                     sizeof(glm::vec2), glm::value_ptr(scale));
  vkCmdPushConstants(cb, ui.pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT,
                     sizeof(glm::vec2), sizeof(glm::vec2),
                     glm::value_ptr(translate));

  for (int i = 0, idxOff = 0, vtxOff = 0; i < drawData->CmdListsCount; ++i) {
    ImDrawList* cmdList = drawData->CmdLists[i];

    for (int j = 0; j < cmdList->CmdBuffer.size(); ++j) {
      ImDrawCmd const* drawCmd = &cmdList->CmdBuffer[j];

      if (drawCmd->UserCallback) {
        drawCmd->UserCallback(cmdList, drawCmd);
      } else {
        VkRect2D scissor;
        scissor.offset.x = (int32_t)(drawCmd->ClipRect.x - displayPos.x) > 0
                             ? (int32_t)(drawCmd->ClipRect.x - displayPos.x)
                             : 0;
        scissor.offset.y = (int32_t)(drawCmd->ClipRect.y - displayPos.y) > 0
                             ? (int32_t)(drawCmd->ClipRect.y - displayPos.y)
                             : 0;
        scissor.extent.width =
          (uint32_t)(drawCmd->ClipRect.z - drawCmd->ClipRect.x);
        scissor.extent.height = (uint32_t)(
          drawCmd->ClipRect.w - drawCmd->ClipRect.y + 1); // FIXME: Why +1 here?

        vkCmdSetScissor(cb, 0, 1, &scissor);
        vkCmdDrawIndexed(cb, drawCmd->ElemCount, 1, idxOff, vtxOff, 0);
      }

      idxOff += drawCmd->ElemCount;
    }

    vtxOff += cmdList->VtxBuffer.Size;
  }

  if (auto result = vkEndCommandBuffer(cb); result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot end UI command buffer"));
  }

  return cb;
} // iris::Renderer::Window::EndFrame

iris::Renderer::Window::Window(Window&& other) noexcept
  : resized(other.resized)
  , window(std::move(other.window))
  , surface(std::move(other.surface))
  , ui(std::move(other.ui)) {
  // Re-bind delegates
  window.OnResize(std::bind(&Window::Resize, this, std::placeholders::_1));
  window.OnClose(std::bind(&Window::Close, this));
} // iris::Renderer::Window::Window

iris::Renderer::Window& iris::Renderer::Window::
operator=(Window&& other) noexcept {
  if (this == &other) return *this;

  resized = other.resized;
  window = std::move(other.window);
  surface = std::move(other.surface);
  ui = std::move(other.ui);

  // Re-bind delegates
  window.OnResize(std::bind(&Window::Resize, this, std::placeholders::_1));
  window.OnClose(std::bind(&Window::Close, this));

  return *this;
} // iris::Renderer::Window::operator=

