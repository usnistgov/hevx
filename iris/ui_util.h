#ifndef HEV_IRIS_UI_UTIL_H_
#define HEV_IRIS_UI_UTIL_H_

#include "config.h"

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "imgui.h"

namespace iris {

template <int N, typename T, glm::qualifier Q>
void Text(int width, char const* name, char const* fmt,
          glm::vec<N, T, Q> const& vec) {
  for (int i = 0; i < N; ++i) {
    ImGui::Text(fmt, vec[i]);
    ImGui::NextColumn();
  }
  for (int i = N; i < width - 1; ++i) {
    ImGui::Text("  ");
    ImGui::NextColumn();
  }
  ImGui::Text(name);
} // Text

template <typename T, glm::qualifier Q>
void Text(int width, char const* name, char const* fmt,
          glm::qua<T, Q> const& qua) {
  ImGui::Text(fmt, qua.w);
  ImGui::NextColumn();
  ImGui::Text(fmt, qua.x);
  ImGui::NextColumn();
  ImGui::Text(fmt, qua.y);
  ImGui::NextColumn();
  ImGui::Text(fmt, qua.z);
  ImGui::NextColumn();
  for (int i = 4; i < width - 1; ++i) {
    ImGui::Text("  ");
    ImGui::NextColumn();
  }
  ImGui::Text(name);
} // Text

template <int C, int R, typename T, glm::qualifier Q>
void Text(int width, char const* name, char const* fmt,
          glm::mat<C, R, T, Q> const& mat) {
  for (int i = 0; i < R; ++i) {
    for (int j = 0; j < C; ++j) {
      ImGui::Text(fmt, mat[j][i]);
      ImGui::NextColumn();
    }
    for (int k = 4; k < width - 1; ++k) {
      ImGui::Text("  ");
      ImGui::NextColumn();
    }
    if (i == 0) {
      ImGui::Text(name);
      ImGui::NextColumn();
    } else {
      ImGui::Text("  ");
      ImGui::NextColumn();
    }
  }
} // Text

} // namespace iris

#endif // HEV_IRIS_UI_UTIL_H_