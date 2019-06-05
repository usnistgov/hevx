#ifndef HEV_IRIS_TRACKBALL_H_
#define HEV_IRIS_TRACKBALL_H_

#include "glm/gtc/quaternion.hpp"
#include "glm/mat4x4.hpp"
#include "glm/gtx/norm.hpp"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "imgui.h"
#include "iris/renderer.h"
#include "iris/types.h"
#include "wsi/input.h"

namespace iris {

class Trackball {
public:
  // These values are tuned to match the old HEV response
  static constexpr float const kSpeed = .5f;  // ether units / s
  static constexpr float const kTwist = 45.f; // degrees / s

  void Update(ImGuiIO const& io) noexcept;

private:
  EulerAngles attitude_{};
  glm::vec3 position_{0.f, 0.f, 0.f};
  glm::vec2 prevMouse_;
}; // class Trackball

inline void Trackball::Update(ImGuiIO const& io) noexcept {
  glm::vec2 const currMouse = static_cast<glm::vec2>(ImGui::GetMousePos()) /
                              static_cast<glm::vec2>(io.DisplaySize);

  if (io.WantCaptureMouse) {
    prevMouse_ = currMouse;
    return;
  }

  glm::vec2 const deltaMouse = currMouse - prevMouse_;

  if (ImGui::IsMouseClicked(wsi::Buttons::kButtonLeft) ||
      ImGui::IsMouseClicked(wsi::Buttons::kButtonMiddle) ||
      ImGui::IsMouseClicked(wsi::Buttons::kButtonRight)) {
    position_ = {0.f, 0.f, 0.f};
    attitude_ = {};
    return;
  } else if (ImGui::IsMouseReleased(wsi::Buttons::kButtonLeft) ||
      ImGui::IsMouseReleased(wsi::Buttons::kButtonMiddle) ||
      ImGui::IsMouseReleased(wsi::Buttons::kButtonRight)) {
    if (glm::length2(deltaMouse) < .00001f) {
      position_ = {0.f, 0.f, 0.f};
      attitude_ = {};
    }
    return;
  }

  if (ImGui::IsMouseDragging(wsi::Buttons::kButtonLeft)) {
    float const dx = deltaMouse.x * kSpeed / io.DeltaTime;
    float const dz = deltaMouse.y * kSpeed / io.DeltaTime;
    position_ = glm::vec3(dx, 0.f, dz);
  } else if (ImGui::IsMouseDragging(wsi::Buttons::kButtonMiddle)) {
    float const dh = deltaMouse.x * kTwist / io.DeltaTime;
    float const dp = -deltaMouse.y * kTwist / io.DeltaTime;
    attitude_.heading = EulerAngles::Heading(glm::radians(dh));
    attitude_.pitch = EulerAngles::Pitch(glm::radians(dp));
  } else if (ImGui::IsMouseDragging(wsi::Buttons::kButtonRight)) {
    float const dy = -deltaMouse.y * kSpeed / io.DeltaTime;
    position_ = glm::vec3(0.f, dy, 0.f);
  }

  if (io.MouseWheel > 0) {
    Renderer::Nav::Rescale(Renderer::Nav::Scale() / 1.05f);
  } else if (io.MouseWheel < 0) {
    Renderer::Nav::Rescale(Renderer::Nav::Scale() * 1.05f);
  }

  glm::vec3 const m(position_ * io.DeltaTime * Renderer::Nav::Response());
  if (m != glm::vec3(0.f, 0.f, 0.f)) {
    Renderer::Nav::Reposition(Renderer::Nav::Position() + m);
  }

  glm::quat const o(attitude_ * io.DeltaTime * Renderer::Nav::Response());
  if (glm::angle(o) != 0.0f) Renderer::Nav::Pivot(o);

  prevMouse_ = currMouse;
} // Trackball::update

} // namespace iris

#endif // HEV_IRIS_TRACKBALL_H_
