#include "io/savg.h"
#include "config.h"

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "components/renderable.h"
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

struct Primitive {
  explicit Primitive(glm::vec4 c)
    : primColor(std::move(c)) {}
  virtual ~Primitive() noexcept {}

  glm::vec4 primColor;

  std::vector<glm::vec3> positions{};
  std::vector<glm::vec4> colors{};
  std::vector<glm::vec3> normals{};
}; // struct Primitive

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
}; // struct Shape

using State = std::variant<std::monostate, Tristrips, Lines, Points, Shape>;

template <typename T, typename It>
tl::expected<T, std::system_error> ParseVec(It start) noexcept {
  T vec;

  for (int i = 0; i < T::length(); ++i, ++start) {
    if (!absl::SimpleAtof(*start, &vec[i])) {
      return tl::unexpected(std::system_error(
        Error::kFileParseFailed, "Invalid value for primitive color"));
    }
  }

  return vec;
} // ParseVec

template <class T>
static T Start(std::vector<std::string_view> const& tokens) noexcept {
  if (tokens.size() == 5) {
    if (auto color = ParseVec<glm::vec4>(tokens.begin() + 1)) {
      return T(*color);
    } else {
      IRIS_LOG_WARN("Error parsing primitive color: {}; ignoring",
                    color.error().what());
      return T();
    }
  } else {
    if (tokens.size() != 1) {
      IRIS_LOG_WARN("Wrong number of values for primitive color: {}; ignoring",
                    tokens.size() - 1);
    }
    return T();
  }
} // Start

void ParseData(std::monostate, std::vector<std::string_view> const&) noexcept {}

std::optional<Renderer::Component::Renderable> End(std::monostate) noexcept {
  return std::nullopt;
}

void ParseData(Primitive& primitive,
               std::vector<std::string_view> const& tokens) noexcept {
  if (tokens.size() != 3 && tokens.size() != 6 && tokens.size() != 7 &&
      tokens.size() != 10) {
    IRIS_LOG_WARN("Wrong number of values for primitive data: {}; ignoring",
                  tokens.size());
    return;
  }

  auto first = tokens.begin();
  if (auto position = ParseVec<glm::vec3>(first)) {
    primitive.positions.push_back(*position);
    first += glm::vec3::length();
  } else {
    IRIS_LOG_WARN("Error parsing xyz for primitive data: {}; ignoring",
                  position.error().what());
  }

  switch (tokens.size()) {
  case 6:
    if (auto normal = ParseVec<glm::vec3>(first)) {
      primitive.normals.push_back(*normal);
    } else {
      IRIS_LOG_WARN("Error parsing xnynzn for primitive data: {}; ignoring",
                    normal.error().what());
    }
    break;
  case 7:
    if (auto color = ParseVec<glm::vec4>(first)) {
      primitive.colors.push_back(*color);
    } else {
      IRIS_LOG_WARN("Error parsing rgba for primitive data: {}; ignoring",
                    color.error().what());
    }
    break;
  case 10:
    if (auto color = ParseVec<glm::vec4>(first)) {
      primitive.colors.push_back(*color);
      first += glm::vec4::length();
    } else {
      IRIS_LOG_WARN("Error parsing rgba for primitive data: {}; ignoring",
                    color.error().what());
      return;
    }

    if (auto normal = ParseVec<glm::vec3>(first)) {
      primitive.normals.push_back(*normal);
    } else {
      IRIS_LOG_WARN("Error parsing xnynzn for primitive data: {}; ignoring",
                    normal.error().what());
    }
    break;
  }
} // ParseData

std::optional<Renderer::Component::Renderable> End(Tristrips&) noexcept {}

std::optional<Renderer::Component::Renderable> End(Lines&) noexcept {}

std::optional<Renderer::Component::Renderable> End(Points&) noexcept {}

void ParseData(Shape&, std::vector<std::string_view> const&) noexcept {}

std::optional<Renderer::Component::Renderable> End(Shape&) noexcept {
  return std::nullopt;
}

tl::expected<std::optional<Renderer::Component::Renderable>, std::system_error>
ParseLine(State& state, std::string_view line) noexcept {
  using namespace std::string_literals;
  IRIS_LOG_ENTER();

  IRIS_LOG_TRACE("line: [{}]", line);
  std::vector<std::string_view> tokens =
    absl::StrSplit(line, " ", absl::SkipWhitespace());

  if (tokens.empty() || tokens[0].empty() || tokens[0][0] == '#') {
    IRIS_LOG_LEAVE();
    return std::nullopt;
  }

  std::optional<Renderer::Component::Renderable> renderable{};

  if (auto nextState = std::visit(
        [&renderable, &tokens](auto&& currState) -> std::optional<State> {
          if (absl::StartsWithIgnoreCase(tokens[0], "END")) {
            renderable = End(currState);
            return State{};
          } else if (absl::StartsWithIgnoreCase(tokens[0], "TRI")) {
            renderable = End(currState);
            return Start<Tristrips>(tokens);
          } else if (absl::StartsWithIgnoreCase(tokens[0], "LIN")) {
            renderable = End(currState);
            return Start<Lines>(tokens);
          } else if (absl::StartsWithIgnoreCase(tokens[0], "POI")) {
            renderable = End(currState);
            return Start<Points>(tokens);
          }

          ParseData(currState, tokens);
          return std::nullopt;
        },
        state)) {
    state = *nextState;
  }

  IRIS_LOG_LEAVE();
  return renderable;
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
      if (auto possibleRenderable = ParseLine(state, line)) {
        if (*possibleRenderable) {
          Renderer::AddRenderable(std::move(**possibleRenderable));
        }
      } else {
        IRIS_LOG_ERROR("Error parsing line: {}", line);
        return tl::unexpected(possibleRenderable.error());
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
