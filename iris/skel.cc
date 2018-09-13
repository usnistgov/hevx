#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "iris/config.h"
#include "iris/renderer/renderer.h"
#include "iris/wsi/window.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable: 4127)
#endif
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif
#include "flags.h"

#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4100)
#endif
#include "google/protobuf/util/json_util.h"
#include "google/protobuf/text_format.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif
#include "iris/protos.h"

iris::Control::Displays CreateDesktopWindow(spdlog::logger& logger) {
  std::string const json = R"({
  "Windows":
  [
    {
      "Name": "desktopWindow",
      "Stereo": false,
      "X": 320,
      "Y": 320,
      "Width": 720,
      "Height": 720,
      "Decoration": true
    }
  ]
})";
  logger.debug("json: {}", json);

  iris::Control::Displays displays;
  if (auto status = google::protobuf::util::JsonStringToMessage(json, &displays);
    status != google::protobuf::util::Status::OK) {
    logger.error("Failed to parse json: {}", status.ToString());
  } else {
    std::string str;
    google::protobuf::TextFormat::PrintToString(displays, &str);
    logger.info("Parsed json into Displays message: {}", str);
  }

  return displays;
}

iris::Control::Displays CreateSimulatorWindows(spdlog::logger& logger) {
  std::string const json = R"({
  "Windows":
  [
    {
      "Name": "frontSimulatorWindow",
      "Stereo": false,
      "X": 320,
      "Y": 320,
      "Width": 720,
      "Height": 720,
      "Decoration": true
    },
    {
      "Name": "leftSimulatorWindow",
      "Stereo": false,
      "X": 320,
      "Y": 320,
      "Width": 720,
      "Height": 720,
      "Decoration": true
    },
    {
      "Name": "floorSimulatorWindow",
      "Stereo": false,
      "X": 320,
      "Y": 320,
      "Width": 720,
      "Height": 720,
      "Decoration": true
    },
    {
      "Name": "consoleWindow",
      "Stereo": false,
      "X": 320,
      "Y": 320,
      "Width": 720,
      "Height": 720,
      "Decoration": true
    }
  ]
})";
  logger.debug("json: {}", json);

  iris::Control::Displays displays;
  if (auto status = google::protobuf::util::JsonStringToMessage(json, &displays);
    status != google::protobuf::util::Status::OK) {
    logger.error("Failed to parse json: {}", status.ToString());
  } else {
    std::string str;
    google::protobuf::TextFormat::PrintToString(displays, &str);
    logger.info("Parsed json into Displays message: {}", str);
  }

  return displays;
}

int main(int argc, char** argv) {
  absl::InitializeSymbolizer(argv[0]);
  absl::InstallFailureSignalHandler({});

  flags::args const args(argc, argv);
  auto const& files = args.positional();

  auto file_sink =
    std::make_shared<spdlog::sinks::basic_file_sink_mt>("skel.log", true);
  file_sink->set_level(spdlog::level::trace);

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::trace);

  spdlog::logger logger("skel", {console_sink, file_sink});
  logger.set_level(spdlog::level::trace);

  logger.info("initialized");
  CreateDesktopWindow(logger);
  CreateSimulatorWindows(logger);

  if (auto error =
        iris::Renderer::Initialize("skel", 0, {console_sink, file_sink})) {
    logger.critical("unable to initialize renderer: {}", error.message());
    std::exit(EXIT_FAILURE);
  }

  for (auto&& file : files) iris::Renderer::LoadFile(file);

  while (iris::Renderer::IsRunning()) { iris::Renderer::Frame(); }

  logger.info("exiting");
}

