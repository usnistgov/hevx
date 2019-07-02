#ifndef HEV_IRIS_SAFE_NUMERIC_H_
#define HEV_IRIS_SAFE_NUMERIC_H_
/*!
\file
\brief \ref iris::SafeNumeric definition.
*/

#include <utility>

namespace iris {

/*!
\brief SafeNumeric is a type-safe numeric class. Its purpose is to mimic
built-in numeric type operations while ensuring different SafeNumeric types
aren't mixed.
*/
template <class T, class Tag>
class SafeNumeric {
public:
  using type = T;

  constexpr SafeNumeric() noexcept = default;
  constexpr explicit SafeNumeric(T value) noexcept
    : value_(std::move(value)) {}

  constexpr explicit operator T() const noexcept { return value_; }

  constexpr T& get() noexcept { return value_; }
  constexpr T const& get() const noexcept { return value_; }

private:
  T value_{};
}; // class SafeNumeric

template <class T, class Tag>
constexpr SafeNumeric<T, Tag> operator+(SafeNumeric<T, Tag> const& a,
                                        SafeNumeric<T, Tag> const& b) noexcept {
  return SafeNumeric<T, Tag>(T(a) + T(b));
}

template <class T, class Tag>
constexpr SafeNumeric<T, Tag> operator-(SafeNumeric<T, Tag> const& a,
                                        SafeNumeric<T, Tag> const& b) noexcept {
  return SafeNumeric<T, Tag>(T(a) - T(b));
}

template <class T, class Tag>
constexpr SafeNumeric<T, Tag> operator*(SafeNumeric<T, Tag> const& a,
                                        SafeNumeric<T, Tag> const& b) noexcept {
  return SafeNumeric<T, Tag>(T(a) * T(b));
}

template <class T, class Tag>
constexpr SafeNumeric<T, Tag> operator/(SafeNumeric<T, Tag> const& a,
                                        SafeNumeric<T, Tag> const& b) noexcept {
  return SafeNumeric<T, Tag>(T(a) / T(b));
}

template <class T, class Tag>
constexpr SafeNumeric<T, Tag>&
operator+=(SafeNumeric<T, Tag>& a, SafeNumeric<T, Tag> const& b) noexcept {
  a = a + b;
  return a;
}

template <class T, class Tag>
constexpr SafeNumeric<T, Tag>&
operator-=(SafeNumeric<T, Tag>& a, SafeNumeric<T, Tag> const& b) noexcept {
  a = a - b;
  return a;
}

template <class T, class Tag>
constexpr SafeNumeric<T, Tag>&
operator*=(SafeNumeric<T, Tag>& a, SafeNumeric<T, Tag> const& b) noexcept {
  a = a * b;
  return a;
}

template <class T, class Tag>
constexpr SafeNumeric<T, Tag>&
operator/=(SafeNumeric<T, Tag>& a, SafeNumeric<T, Tag> const& b) noexcept {
  a = a / b;
  return a;
}

template <class T, class Tag>
SafeNumeric<T, Tag>& operator++(SafeNumeric<T, Tag>& a) noexcept {
  a.get()++;
  return a;
}

template <class T, class Tag>
SafeNumeric<T, Tag>& operator--(SafeNumeric<T, Tag>& a) noexcept {
  a.get()--;
  return a;
}

template <class T, class Tag>
SafeNumeric<T, Tag> operator++(SafeNumeric<T, Tag>& a, int) noexcept {
  auto tmp(a);
  ++a;
  return tmp;
}

template <class T, class Tag>
SafeNumeric<T, Tag> operator--(SafeNumeric<T, Tag>& a, int) noexcept {
  auto tmp(a);
  --a;
  return tmp;
}

template <class T, class Tag>
bool operator==(SafeNumeric<T, Tag> const& a,
                SafeNumeric<T, Tag> const& b) noexcept {
  return a.get() == b.get();
}

template <class T, class Tag>
bool operator!=(SafeNumeric<T, Tag> const& a,
                SafeNumeric<T, Tag> const& b) noexcept {
  return a.get() != b.get();
}

template <class T, class Tag>
bool operator<(SafeNumeric<T, Tag> const& a,
               SafeNumeric<T, Tag> const& b) noexcept {
  return a.get() < b.get();
}

template <class T, class Tag>
bool operator<=(SafeNumeric<T, Tag> const& a,
                SafeNumeric<T, Tag> const& b) noexcept {
  return a.get() <= b.get();
}

template <class T, class Tag>
bool operator>(SafeNumeric<T, Tag> const& a,
               SafeNumeric<T, Tag> const& b) noexcept {
  return a.get() > b.get();
}

template <class T, class Tag>
bool operator>=(SafeNumeric<T, Tag> const& a,
                SafeNumeric<T, Tag> const& b) noexcept {
  return a.get() >= b.get();
}

} // namespace iris

#endif // HEV_IRIS_SAFE_NUMERIC_H_
