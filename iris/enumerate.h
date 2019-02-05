#ifndef HEV_IRIS_ENUMERATE_H_
#define HEV_IRIS_ENUMERATE_H_

#include <cstddef>
#include <tuple>

namespace iris {

template <class C, class I = decltype(std::begin(std::declval<C>())),
          class = decltype(std::end(std::declval<C>()))>
constexpr auto enumerate(C&& c) noexcept(noexcept(std::begin(c)) &&
                                         noexcept(std::end(c))) {
  // clang-format off
  struct iterator {
    std::size_t n;
    I i;
    bool operator!=(iterator const& o) const noexcept { return i != o.i; }
    void operator++() noexcept(noexcept(++i)) { ++n; ++i; }
    auto operator*() const noexcept(noexcept(*i)) { return std::tie(n, *i); }
  };

  struct wrapper {
    C i;
    iterator begin() noexcept(noexcept(std::begin(i))) {return {0,std::begin(i)};}
    iterator end() noexcept(noexcept(std::end(i))) {return{0, std::end(i)};}
  };
  // clang-format on

  return wrapper{std::forward<C>(c)};
} // enumerate

} // namespace iris

#endif // HEV_IRIS_ENUMERATE_H_
