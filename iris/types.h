#ifndef HEV_IRIS_TYPES_H_
#define HEV_IRIS_TYPES_H_

#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"
#include "iris/safe_numeric.h"

namespace iris {

/*!
\brief EulerAngles is a struct that holds intrinsic Tait-Bryan angles. These
are angles that represent a sequence of model axis rotations.
\see https://en.wikipedia.org/wiki/Euler_angles#Tait%E2%80%93Bryan_angles

We are using 'Heading' in place of 'Yaw'.
*/
struct EulerAngles {
  //! \brief Rotation amount around the vertical model axis.
  using Heading = SafeNumeric<float, struct HeadingTag>;
  //! \brief Rotation amount around the side model axis.
  using Pitch = SafeNumeric<float, struct PitchTag>;
  //! \brief Rotation amount around the forward model axis.
  using Roll = SafeNumeric<float, struct RollTag>;

  Heading heading{0.f};
  Pitch pitch{0.f};
  Roll roll{0.f};

  constexpr EulerAngles() noexcept = default;

  constexpr EulerAngles(Heading h, Pitch p, Roll r) noexcept
    : heading(std::move(h))
    , pitch(std::move(p))
    , roll(std::move(r)) {}

  explicit operator glm::quat() const noexcept {
    return glm::quat(glm::vec3(float(pitch), float(roll), float(heading)));
  }
}; // struct EulerAngles

/*!
\brief Component-wise multiplication of an EulerAngles by a single scalar value.
\param[in] a
\param[in] s the scalar value to multiply heading, pitch, and roll by.
*/
inline EulerAngles operator*(EulerAngles a, float s) noexcept {
  a.heading *= EulerAngles::Heading(s);
  a.pitch *= EulerAngles::Pitch(s);
  a.roll *= EulerAngles::Roll(s);
  return a;
};

/*!
\brief Component-wise division of an EulerAngles by a single scalar value.
\param[in] a
\param[in] s the scalar value to divide heading, pitch, and roll by.
*/
inline EulerAngles operator/(EulerAngles a, float s) noexcept {
  a.heading /= EulerAngles::Heading(s);
  a.pitch /= EulerAngles::Pitch(s);
  a.roll /= EulerAngles::Roll(s);
  return a;
};

/*!
\brief Component-wise multiplication of an EulerAngles by a single scalar value.
\param[in,out] a
\param[in] s the scalar value to multiply heading, pitch, and roll by.
*/
inline EulerAngles& operator*=(EulerAngles& a, float s) noexcept {
  a = a * s;
  return a;
};

/*!
\brief Component-wise division of an EulerAngles by a single scalar value.
\param[in,out] a
\param[in] s the scalar value to divide heading, pitch, and roll by.
*/
inline EulerAngles& operator/=(EulerAngles& a, float s) noexcept {
  a = a / s;
  return a;
};

} // namespace iris

#endif // HEV_IRIS_TYPES_H_
