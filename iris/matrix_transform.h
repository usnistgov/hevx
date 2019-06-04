#ifndef IRIS_MATRIX_TRANSFORM_H_
#define IRIS_MATRIX_TRANSFORM_H_

#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "glm/gtx/norm.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"

namespace iris {

class MatrixTransform {
public:
  glm::vec3 const& getPosition() const noexcept { return position_; }
  void setPosition(glm::vec3 position) noexcept {
    position_ = std::move(position);
    updateMatrix();
  }

  glm::quat const& getAttitude() const noexcept { return attitude_; }
  void setAttitude(glm::quat attitude) {
    attitude_ = std::move(attitude);
    updateMatrix();
  }

  glm::vec3 const& getScale() const noexcept { return scale_; }
  void setScale(glm::vec3 scale) noexcept {
    scale_ = std::move(scale);
    updateMatrix();
  }

  glm::mat4 const& getMatrix() const noexcept { return matrix_; }
  void setMatrix(glm::mat4 matrix) noexcept {
    matrix_ = std::move(matrix);
    updatePosAttScale();
  }

  glm::mat4 computeLocalToWorld(glm::mat4 const& matrix) const noexcept {
    return matrix * matrix_;
  }

private:
  glm::mat4 matrix_{1.f};
  glm::quat attitude_{};
  glm::vec3 position_{0.f, 0.f, 0.f};
  glm::vec3 scale_{1.f, 1.f, 1.f};

  void updateMatrix() {
    matrix_ = glm::scale(glm::mat4(1.f), scale_) * glm::mat4_cast(attitude_);
    matrix_ = glm::translate(matrix_, position_);
  }

  void updatePosAttScale() {
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(matrix_, scale_, attitude_, position_, skew, perspective);
  }
}; // class MatrixTransform

} // namespace iris

#endif // IRIS_MATRIX_TRANSFORM_H_

