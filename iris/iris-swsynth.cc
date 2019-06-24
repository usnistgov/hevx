#define _USE_MATH_DEFINES

#include "iris/config.h"

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/parse.h"
#include "gsl/gsl"
#include "imgui.h"
#include "iris/protos.h"
#include "iris/renderer.h"
#include "portaudio.h"
#include "spdlog/logger.h"
#include "spdlog/sinks/ansicolor_sink.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include <cmath>

struct Oscillator {
  static constexpr float const k2PI = 2.f * gsl::narrow_cast<float>(M_PI);
  static constexpr float const kZero = 0.f;

  constexpr Oscillator(float f, float sR) noexcept
    : frequency(f)
    , sampleRate(sR) {}

  float const frequency;
  float const sampleRate;
  float phase{0.f};

  float operator()() noexcept {
    phase += k2PI * frequency / sampleRate;
    while (phase >= k2PI) phase -= k2PI;
    while (phase < kZero) phase += k2PI;
    return phase;
  }
}; // struct Oscillator

class Sine {
public:
  float operator()(Oscillator& osc) noexcept { return std::sin(osc()); }
}; // class Sine

class Square {
public:
  constexpr Square(int nH = 0) noexcept
    : numHarmonics_(nH) {}

  float operator()(Oscillator& osc) noexcept {
    if (numHarmonics_ == 0) {
      float const halfSampleRate = osc.sampleRate * .5f;
      while (osc.frequency * float(numHarmonics_ * 2 - 1) < halfSampleRate) {
        numHarmonics_++;
      }
      numHarmonics_--;
    }

    float const phase = osc();
    float ret = 0.f;

    for (int i = 1; i <= numHarmonics_; ++i) {
      float const j = float(i * 2 - 1);
      ret += std::sin(phase * j) / j;
    }

    return ret;
  }

private:
  int numHarmonics_;
}; // class Square

class Saw {
public:
  constexpr Saw(int nH = 0) noexcept
    : numHarmonics_(nH) {}

  float operator()(Oscillator& osc) noexcept {
    if (numHarmonics_ == 0) {
      float frequency = osc.frequency;
      while (frequency < osc.sampleRate) {
        numHarmonics_++;
        frequency *= 2.f;
      }
    }

    float const phase = osc();
    float ret = 0.f;

    for (int i = 1; i <= numHarmonics_; ++i) {
      ret += std::sin(phase * float(i)) / float(i);
    }

    return ret;
  }

private:
  int numHarmonics_;
}; // class Saw

class Triangle {
public:
  constexpr Triangle(int nH = 0) noexcept
    : numHarmonics_(nH) {}

  float operator()(Oscillator& osc) noexcept {
    if (numHarmonics_ == 0) {
      float const halfSampleRate = osc.sampleRate * .5f;
      while (osc.frequency * float(numHarmonics_ * 2 - 1) < halfSampleRate) {
        numHarmonics_++;
      }
      numHarmonics_--;
    }

    float const phase = osc();
    bool subtract = true;
    float ret = 0.f;

    for (int i = 1; i <= numHarmonics_; ++i) {
      float const j = float(i * 2 - 1);
      ret += (subtract ? -1.f : 1.f) * std::sin(phase * j) / (j * j);
      subtract = !subtract;
    }

    return ret * 4.f / float(M_PI);
  }

private:
  int numHarmonics_;
}; // class Triangle

struct AudioData {
  float const sampleRate{44100.f};

  Oscillator osc{440.f, 44100.f};

  Sine sine;
  Square square;
  Saw saw;
  Triangle triangle;

  enum class WaveType : int {
    kSine,
    kSquare,
    kSaw,
    kTriangle
  } waveType = WaveType::kSine;

  static char const* const WaveLabels[];
}; // struct AudioData

char const* const AudioData::WaveLabels[] = {"Sine", "Square", "Saw",
                                             "Triangle"};

static int Callback(void const* inputBuffer [[maybe_unused]],
                    void* outputBuffer, unsigned long framesPerBuffer,
                    PaStreamCallbackTimeInfo const* timeInfo,
                    PaStreamCallbackFlags statusFlags [[maybe_unused]],
                    void* userData) noexcept {
  auto audioData = reinterpret_cast<AudioData*>(userData);

  float const startTime =
    gsl::narrow_cast<float const>(timeInfo->outputBufferDacTime) *
    audioData->sampleRate;

  auto fltOutput = static_cast<float*>(outputBuffer);
  for (unsigned long i = 0; i < framesPerBuffer; ++i) {
    float sample = 0.f;

    switch (audioData->waveType) {
    case AudioData::WaveType::kSine:
      sample = audioData->sine(audioData->osc);
      break;
    case AudioData::WaveType::kSquare:
      sample = audioData->square(audioData->osc);
      break;
    case AudioData::WaveType::kSaw:
      sample = audioData->saw(audioData->osc);
      break;
    case AudioData::WaveType::kTriangle:
      sample = audioData->triangle(audioData->osc);
      break;
    }

    *fltOutput++ = sample * .2f;
    *fltOutput++ = sample * .2f;
  }

  return 0;
} // paCallback

#if PLATFORM_WINDOWS
extern "C" {
_declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}

#include <Windows.h>
#include <shellapi.h>

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
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

  auto const positional = absl::ParseCommandLine(argc, argv);

  auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
    "iris-raytracer.log", true);
  file_sink->set_level(spdlog::level::trace);

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::trace);

  spdlog::logger logger("iris-swsynth", {console_sink, file_sink});
  logger.set_level(spdlog::level::trace);

  logger.info("Logging initialized");

  if (auto result = iris::Renderer::Initialize(
        "iris-viewer",
        iris::Renderer::Options::kReportDebugMessages |
          iris::Renderer::Options::kEnableValidation,
        {console_sink, file_sink}, 0);
      !result) {
    logger.critical("cannot initialize renderer: {}", result.error().what());
    std::exit(EXIT_FAILURE);
  }

  // Create a custom Window control message that turns off the Debug UI.
  iris::Control::Control control;
  control.mutable_window()->set_name("synthWindow");
  control.mutable_window()->set_is_stereo(false);
  control.mutable_window()->set_x(100);
  control.mutable_window()->set_y(100);
  control.mutable_window()->set_width(720);
  control.mutable_window()->set_height(800);
  control.mutable_window()->set_show_system_decoration(true);
  control.mutable_window()->set_show_ui(true);

  if (auto result = iris::Renderer::ProcessControlMessage(control); !result) {
    logger.critical("cannot load window: {}", result.error().what());
    std::exit(EXIT_FAILURE);
  }

  PaError err = Pa_Initialize();
  if (err != paNoError) {
    logger.error("error initializing audio: {}", Pa_GetErrorText(err));
    std::exit(EXIT_FAILURE);
  }

  AudioData audioData;
  PaStream* stream;

  err = Pa_OpenDefaultStream(
    &stream,
    0,                            // input channels
    2,                            // output channels
    paFloat32,                    // output format
    audioData.sampleRate,         // sample rate
    paFramesPerBufferUnspecified, // frames per buffer: let PA pick
    Callback,                     // callback function
    &audioData                    // user data
  );

  if (err != paNoError) {
    logger.error("error opening default stream: {}", Pa_GetErrorText(err));
    std::exit(EXIT_FAILURE);
  }

  while (iris::Renderer::IsRunning()) {
    iris::Renderer::BeginFrame();

    if (ImGui::Begin("Synth")) {
      ImGui::BeginGroup();
      ImGui::TextColored(ImVec4(.4f, .2f, 1.f, 1.f), "Stream");
      PaStreamInfo const* streamInfo = Pa_GetStreamInfo(stream);
      ImGui::LabelText("Sample Rate", "%.3f", streamInfo->sampleRate);
      ImGui::LabelText("Input Latency", "%.3f", streamInfo->inputLatency);
      ImGui::LabelText("Output Latency", "%.3f", streamInfo->outputLatency);
      ImGui::LabelText("CPU Load", "%.3f", Pa_GetStreamCpuLoad(stream));
      ImGui::EndGroup();

      ImGui::BeginGroup();
      ImGui::TextColored(ImVec4(.4f, .2f, 1.f, 1.f), "Controls");

      static bool playing = false;
      if (ImGui::Button((playing ? "Stop" : "Play"))) {
        if (playing) {
          err = Pa_StopStream(stream);
          if (err != paNoError) {
            logger.error("error stopping stream: {}", Pa_GetErrorText(err));
            break;
          }
        } else {
          err = Pa_StartStream(stream);
          if (err != paNoError) {
            logger.error("error starting stream: {}", Pa_GetErrorText(err));
            break;
          }
        }
        playing = !playing;
      }

      ImGui::Combo("Wave", reinterpret_cast<int*>(&audioData.waveType),
                   AudioData::WaveLabels, 4);

      ImGui::EndGroup();
    }
    ImGui::End(); // Synth

    iris::Renderer::EndFrame();
  }

  err = Pa_CloseStream(stream);
  if (err != paNoError) {
    logger.error("error closing stream: {}", Pa_GetErrorText(err));
    std::exit(EXIT_FAILURE);
  }

  err = Pa_Terminate();
  if (err != paNoError) {
    logger.error("error terminating audio: {}", Pa_GetErrorText(err));
  }

  logger.info("exiting");
}
