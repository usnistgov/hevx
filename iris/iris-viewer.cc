#include "absl/container/fixed_array.h"
#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "fmt/format.h"
#include "imgui.h"
#include "iris/config.h"
#include "iris/renderer/renderer.h"
#include "iris/wsi/window.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include "spdlog/sinks/base_sink.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif
#include "flags.h"
#include <mutex>

template <class Mutex>
class iris_viewer_sink : public spdlog::sinks::base_sink<Mutex> {
  ImGuiTextBuffer textBuffer_{};
  ImVector<int> lineOffsets_{};
  bool scrollToBottom_{false};

public:
  void Draw() {
    if (ImGui::Button("Clear")) {
      textBuffer_.clear();
      lineOffsets_.clear();
      lineOffsets_.push_back(0);
    }

    ImGui::Separator();
    ImGui::BeginChild("scrolling", glm::vec2{0, 0}, false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, glm::vec2{0, 0});

    char const* bufferStart = textBuffer_.begin();
    char const* bufferEnd = textBuffer_.end();

    ImGuiListClipper clipper;
    clipper.Begin(lineOffsets_.Size);

    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        char const* lineStart = bufferStart + lineOffsets_[i];
        char const* lineEnd = (i + 1 < lineOffsets_.Size)
                                ? (bufferStart + lineOffsets_[i + 1] - 1)
                                : bufferEnd;
        ImGui::TextUnformatted(lineStart, lineEnd);
      }
    }
    clipper.End();

    ImGui::PopStyleVar();
    if (scrollToBottom_) ImGui::SetScrollHereY(1.f);
    scrollToBottom_ = false;
    ImGui::EndChild();
  }

protected:
  void sink_it_(spdlog::details::log_msg const& msg) override {
    fmt::memory_buffer formatted;
    spdlog::sinks::sink::formatter_->format(msg, formatted);

    int oldSize = textBuffer_.size();
    textBuffer_.append(formatted.begin(), formatted.end());

    for (int newSize = textBuffer_.size(); oldSize < newSize; ++oldSize) {
      if (textBuffer_[oldSize] == '\n') lineOffsets_.push_back(oldSize + 1);
    }

    scrollToBottom_ = true;
  } // sink_it_

  void flush_() override {} // flush_
};                          // class iris_viewer_sink

using iris_viewer_sink_mt = iris_viewer_sink<std::mutex>;
using iris_viewer_sink_st = iris_viewer_sink<spdlog::details::null_mutex>;

#if PLATFORM_WINDOWS
extern "C" {
_declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}

#include <Windows.h>
#include <shellapi.h>

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  // Oh my goodness
  char* cmdLine = ::GetCommandLineA();
  int argc = 1;
  char* argv[128]; // 128 command line argument max
  argv[0] = cmdLine;

  for (char* p = cmdLine; *p; ++p) {
    if (*p == ' ') {
      *p++ = '\0';
      if (*(p + 1)) argv[argc++] = p;
    }
  }

#else

int main(int argc, char** argv) {

#endif

  absl::InitializeSymbolizer(argv[0]);
  absl::InstallFailureSignalHandler({});

  flags::args const args(argc, argv);
  auto const& files = args.positional();

  auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
    "iris-viewer.log", true);
  file_sink->set_level(spdlog::level::trace);

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::trace);

  auto ui_sink = std::make_shared<iris_viewer_sink_mt>();
  ui_sink->set_level(spdlog::level::warn);

  spdlog::logger logger("iris-viewer", {console_sink, file_sink, ui_sink});
  logger.set_level(spdlog::level::trace);

  logger.info("Logging initialized");

  if (auto error = iris::Renderer::Initialize(
        "iris-viewer",
        iris::Renderer::Options::kReportDebugMessages |
          iris::Renderer::Options::kUseValidationLayers,
        0, {console_sink, file_sink, ui_sink});
      error.code()) {
    logger.critical("cannot initialize renderer: {}", error.what());
    std::exit(EXIT_FAILURE);
  }

  logger.info("Renderer initialized. {} files specified on command line.", files.size());

  for (auto&& file : files) {
    logger.info("Loading {}", file);
    if (auto error = iris::Renderer::LoadFile(file)) {
      logger.error("Error loading {}: {}", file, error.message());
    }
  }

  bool showStatusWindow = false;
  bool showDemoWindow = false;
  absl::FixedArray<float> frameTimes(100);

  while (iris::Renderer::IsRunning()) {
    if (!iris::Renderer::BeginFrame()) continue;

    if (ImGui::IsKeyPressed(iris::wsi::Keys::kS)) {
      showStatusWindow = !showStatusWindow;
    }
    if (ImGui::IsKeyPressed(iris::wsi::Keys::kD)) {
      showDemoWindow = !showDemoWindow;
    }

    ImGuiIO& io = ImGui::GetIO();
    frameTimes[ImGui::GetFrameCount() % frameTimes.size()] =
      1000.f * io.DeltaTime;

    if (showStatusWindow) {
      if (ImGui::Begin("Status", &showStatusWindow)) {
        ImGui::Text("Last Frame %.3f ms Frame %d", 1000.f * io.DeltaTime,
                    ImGui::GetFrameCount());
        ImGui::PlotHistogram(
          "Frame Times", frameTimes.data(), frameTimes.size(), 0,
          fmt::format("Average {:.3f} ms", 1000.f / io.Framerate).c_str(), 0.f,
          100.f, glm::vec2{0, 50});

        ui_sink->Draw();
      }
      ImGui::End(); // Status
    }

    if (showDemoWindow) ImGui::ShowDemoWindow(&showDemoWindow);

    iris::Renderer::EndFrame();
  }

  iris::Renderer::Shutdown();
  logger.info("exiting");
}
