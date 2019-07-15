#include "io/savg.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "config.h"
#include "error.h"
#include "expected.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "io/read_file.h"
#include "logging.h"
#include "renderer.h"
#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace iris::savg {

template <typename T, typename It>
tl::expected<T, std::system_error> ParseVec(It first, It last) noexcept {
  T vec;

  for (int i = 0; i < T::length(); ++i) {
    if (!absl::SimpleAtof(*first++, &vec[i])) {
      return tl::unexpected(std::system_error(
        Error::kFileParseFailed, "Invalid value for primitive color"));
    }
  }

  return vec;
} // ParseVec

struct Primitive {
  explicit Primitive(glm::vec4 c)
    : primColor(std::move(c)) {}
  virtual ~Primitive() noexcept {}

  glm::vec4 primColor;

  std::vector<glm::vec3> positions;
  std::vector<glm::vec4> colors;
  std::vector<glm::vec3> normals;

  template <class T>
  static T Start(std::vector<std::string_view> const& tokens) noexcept {
    if (tokens.size() == 5) {
      if (auto color = ParseVec<glm::vec4>(tokens.begin() + 1, tokens.end())) {
        return T(*color);
      } else {
        IRIS_LOG_WARN("Error parsing primitive color: {}; ignoring",
                      color.error().what());
        return T();
      }
    } else {
      if (tokens.size() != 1) {
        IRIS_LOG_WARN(
          "Wrong number of values for primitive color: {}; ignoring",
          tokens.size() - 1);
      }
      return T();
    }
  } // Start

  virtual void ParseData(std::vector<std::string_view> const& tokens) noexcept {
    if (tokens.size() != 3 && tokens.size() != 6 && tokens.size() != 7 &&
        tokens.size() != 10) {
      IRIS_LOG_WARN("Wrong number of values for primitive data: {}; ignoring",
                    tokens.size());
      return;
    }

    auto first = tokens.begin(), last = first + glm::vec3::length();
    if (auto position = ParseVec<glm::vec3>(first, last)) {
      positions.push_back(*position);
      first = last;
    } else {
      IRIS_LOG_WARN("Error parsing xyz for primitive data: {}; ignoring",
                    position.error().what());
    }

    switch (tokens.size()) {
    case 6:
      last = first + glm::vec3::length();
      if (auto normal = ParseVec<glm::vec3>(first, last)) {
        normals.push_back(*normal);
      } else {
        IRIS_LOG_WARN("Error parsing xnynzn for primitive data: {}; ignoring",
                      normal.error().what());
      }
      break;
    case 7:
      last = first + glm::vec4::length();
      if (auto color = ParseVec<glm::vec4>(first, last)) {
        colors.push_back(*color);
      } else {
        IRIS_LOG_WARN("Error parsing rgba for primitive data: {}; ignoring",
                      color.error().what());
      }
      break;
    case 10:
      last = first + glm::vec4::length();
      if (auto color = ParseVec<glm::vec4>(first, last)) {
        colors.push_back(*color);
        first = last;
      } else {
        IRIS_LOG_WARN("Error parsing rgba for primitive data: {}; ignoring",
                      color.error().what());
        return;
      }

      last = first + glm::vec3::length();
      if (auto normal = ParseVec<glm::vec3>(first, last)) {
        normals.push_back(*normal);
      } else {
        IRIS_LOG_WARN("Error parsing xnynzn for primitive data: {}; ignoring",
                      normal.error().what());
      }
      break;
    }
  } // ParseData
};  // struct Primitive

struct Tristrips final : public Primitive {
  Tristrips(glm::vec4 c = glm::vec4(0.f, 0.f, 0.f, 0.f))
    : Primitive(std::move(c)) {}
}; // struct Tristrips

struct Lines final : public Primitive {
  Lines(glm::vec4 c = glm::vec4(0.f, 0.f, 0.f, 0.f))
    : Primitive(std::move(c)) {}
}; // struct Lines

struct Points final : public Primitive {
  Points(glm::vec4 c = glm::vec4(0.f, 0.f, 0.f, 0.f))
    : Primitive(std::move(c)) {}
}; // struct Points

struct Shape {
  enum class Geometries { kAABBs, kTriangles };

  Geometries geometry;
}; // struct Shape

std::string to_string(Shape::Geometries geometry) noexcept {
  using namespace std::string_literals;
  switch (geometry) {
  case Shape::Geometries::kAABBs: return "aabbs"s;
  case Shape::Geometries::kTriangles: return "triangles"s;
  default: return "bad enum value"s;
  }
}

using State = std::variant<std::monostate, Tristrips, Lines, Points, Shape>;

tl::expected<void, std::system_error>
ParseLine(State& state, std::string_view line) noexcept {
  using namespace std::string_literals;
  IRIS_LOG_ENTER();

  IRIS_LOG_TRACE("line: [{}]", line);
  std::vector<std::string_view> tokens =
    absl::StrSplit(line, " ", absl::SkipWhitespace());

  if (tokens.empty() || tokens[0].empty() || tokens[0][0] == '#') {
    IRIS_LOG_LEAVE();
    return {};
  }

  auto nextState = match(
    state,
    [=](std::monostate) -> std::optional<State> {
      if (absl::StartsWithIgnoreCase(tokens[0], "TRI")) {
        return Primitive::Start<Tristrips>(tokens);
      } else if (absl::StartsWithIgnoreCase(tokens[0], "LIN")) {
        return Primitive::Start<Lines>(tokens);
      } else if (absl::StartsWithIgnoreCase(tokens[0], "POI")) {
        return Primitive::Start<Points>(tokens);
      }

      IRIS_LOG_WARN("Unsupported SAVG keyword: {}", tokens[0]);
      return std::nullopt;
    },
    [=](Tristrips& tristrips) -> std::optional<State> {
      if (absl::StartsWithIgnoreCase(tokens[0], "END")) {
        IRIS_LOG_DEBUG("Finished parsing tristrips with {} positions",
                       tristrips.positions.size());
        return State{};
      } else if (absl::StartsWithIgnoreCase(tokens[0], "TRI")) {
        return Primitive::Start<Tristrips>(tokens);
      } else if (absl::StartsWithIgnoreCase(tokens[0], "LIN")) {
        return Primitive::Start<Lines>(tokens);
      } else if (absl::StartsWithIgnoreCase(tokens[0], "POI")) {
        return Primitive::Start<Points>(tokens);
      }

      tristrips.ParseData(tokens);
      return std::nullopt;
    },
    [=](Lines& lines) -> std::optional<State> {
      if (absl::StartsWithIgnoreCase(tokens[0], "END")) {
        IRIS_LOG_DEBUG("Finished parsing lines with {} positions",
                       lines.positions.size());
        return State{};
      } else if (absl::StartsWithIgnoreCase(tokens[0], "TRI")) {
        return Primitive::Start<Tristrips>(tokens);
      } else if (absl::StartsWithIgnoreCase(tokens[0], "LIN")) {
        return Primitive::Start<Lines>(tokens);
      } else if (absl::StartsWithIgnoreCase(tokens[0], "POI")) {
        return Primitive::Start<Points>(tokens);
      }

      lines.ParseData(tokens);
      return std::nullopt;
    },
    [=](Points& points) -> std::optional<State> {
      if (absl::StartsWithIgnoreCase(tokens[0], "END")) {
        IRIS_LOG_DEBUG("Finished parsing points with {} positions",
                       points.positions.size());
        return State{};
      } else if (absl::StartsWithIgnoreCase(tokens[0], "TRI")) {
        return Primitive::Start<Tristrips>(tokens);
      } else if (absl::StartsWithIgnoreCase(tokens[0], "LIN")) {
        return Primitive::Start<Lines>(tokens);
      } else if (absl::StartsWithIgnoreCase(tokens[0], "POI")) {
        return Primitive::Start<Points>(tokens);
      }

      points.ParseData(tokens);
      return std::nullopt;
    },
    [=](Shape const& shape) -> std::optional<State> {
      if (absl::StartsWithIgnoreCase(tokens[0], "END")) {
        IRIS_LOG_DEBUG("Finished parsing {} shape", to_string(shape.geometry));
        return State{};
      }
      return std::nullopt;
    });

  if (nextState) state = *nextState;

  IRIS_LOG_LEAVE();
  return {};
} // ParseLine

} // namespace iris::savg

namespace iris::io {

tl::expected<void, std::system_error> static ParseSAVG(
  std::vector<std::byte> const& bytes,
  std::filesystem::path const& path [[maybe_unused]] = "") noexcept {
  IRIS_LOG_ENTER();

  savg::State state;

  std::size_t const nBytes = bytes.size();
  for (std::size_t prev = 0, curr = 0; prev < nBytes; ++curr) {
    if (bytes[curr] == std::byte('\n') || curr == nBytes - 1) {
      std::string line(reinterpret_cast<char const*>(&bytes[prev]),
                       reinterpret_cast<char const*>(&bytes[curr]));
      if (auto result = ParseLine(state, line); !result) {
        IRIS_LOG_ERROR("Error parsing line: {}", line);
        return tl::unexpected(result.error());
      }
      prev = curr + 1;
    }
  }

  IRIS_LOG_LEAVE();
  return {};
} // ParseSAVG

} // namespace iris::io

std::function<std::system_error(void)>
iris::io::LoadSAVG(std::filesystem::path const& path) noexcept {
  IRIS_LOG_ENTER();

  if (auto&& bytes = ReadFile(path)) {
    if (auto ret = ParseSAVG(*bytes, path); !ret) {
      IRIS_LOG_ERROR("Error parsing SAVG: {}", ret.error().what());
      return []() { return std::system_error(Error::kFileLoadFailed); };
    }
  } else {
    IRIS_LOG_LEAVE();
    IRIS_LOG_ERROR("Error reading {}: {}", path.string(), bytes.error().what());
    return []() { return std::system_error(Error::kFileLoadFailed); };
  }

  IRIS_LOG_LEAVE();
  return []() { return std::system_error(Error::kNone); };
} // iris::io::LoadGLTF
