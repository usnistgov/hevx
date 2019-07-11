#ifndef HEV_IRIS_TYPES_H_
#define HEV_IRIS_TYPES_H_

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_set.h"
#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"
#include "iris/error.h"
#include "iris/safe_numeric.h"
#include <mutex>

namespace iris {

template <class Tag>
struct ComponentID {
public:
    using id_type = std::uint32_t;

    constexpr ComponentID() noexcept = default;
    constexpr explicit ComponentID(id_type id) noexcept
      : id_(std::move(id)) {}

    id_type& operator()() noexcept { return id_; }
    id_type const& operator()() const noexcept { return id_; }

    friend bool operator==(ComponentID const& lhs,
                           ComponentID const& rhs) noexcept {
      return lhs.id_ == rhs.id_;
    }

    friend bool operator<(ComponentID const& lhs,
                          ComponentID const& rhs) noexcept {
      return lhs.id_ < rhs.id_;
    }

private:
    id_type id_{UINT32_MAX};
}; // struct ComponentID

template <class ID, class T>
struct ComponentSystem {
  ID Insert(T component) {
    std::lock_guard<decltype(mutex)> lck(mutex);
    ID const newID(nextID++);
    components.emplace(newID, std::move(component));
    return newID;
  }

  std::optional<T> Remove(ID const& id) {
    std::lock_guard<decltype(mutex)> lck(mutex);
    if (auto pos = components.find(id); pos == components.end()) {
      return {};
    } else {
      auto old = pos->second;
      components.erase(pos);
      return old;
    }
  }

  std::size_t size() const noexcept { return components.size(); }

  std::mutex mutex{};
  typename ID::id_type nextID{0};
  absl::flat_hash_map<ID, T> components;
}; // struct ComponentSystem

template <class ID, class T>
struct UniqueComponentSystem {
  tl::expected<ID, std::system_error> Insert(T component) {
    std::lock_guard<decltype(mutex)> lck(mutex);
    if (auto&& [position, inserted] = uniques.insert(std::move(component));
      inserted) {
      ID const newID(nextID++);
      components.insert(std::make_pair(newID, std::addressof(*position)));
      return newID;
    } else {
      for (auto&& comp : components) {
        if (std::addressof(*position) == comp.second) return comp.first;
      }
      return tl::unexpected(std::system_error(Error::kUniqueComponentNotMapped));
    }
  }

  tl::expected<T, std::system_error> Remove(ID const& id) {
    std::lock_guard<decltype(mutex)> lck(mutex);
    if (auto pos = components.find(id); pos == components.end()) {
      return {};
    } else {
      auto old = uniques.find(*pos->second);
      if (old == uniques.end()) {
        return tl::unexpected(std::system_error(Error::kUniqueComponentNotMapped));
      }

      uniques.erase(old++);
      components.erase(pos);
      return *old;
    }
  }

  std::size_t size() const noexcept { return components.size(); }

  std::mutex mutex{};
  typename ID::id_type nextID{0};
  absl::node_hash_set<T> uniques;
  absl::flat_hash_map<ID, T const*> components;
}; // struct UniqueComponentSystem

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
