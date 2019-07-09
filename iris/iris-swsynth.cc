#define _USE_MATH_DEFINES

#include "iris/config.h"

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/parse.h"
#include "gsl/gsl"
#include "imgui.h"
#include "iris/protos.h"
#include "iris/renderer.h"
#include "iris/wsi/input.h"
#include "portaudio.h"
#include "spdlog/logger.h"
#include "spdlog/sinks/ansicolor_sink.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include <cmath>

namespace synth {

template <class T>
constexpr inline T kPI = static_cast<T>(M_PI);

using Frequency = iris::SafeNumeric<float, struct FrequencyTag>;
using SampleRate = iris::SafeNumeric<float, struct SampleRateTag>;
using Phase = iris::SafeNumeric<float, struct PhaseTag>;
using Amplitude = iris::SafeNumeric<float, struct AmplitudeTag>;
using Ticks = std::chrono::duration<int>;
using Seconds = std::chrono::duration<float>;

struct Oscillator {
  constexpr Oscillator(Frequency f, SampleRate sR) noexcept
    : frequency(f)
    , sampleRate(sR) {}

  Frequency const frequency;
  SampleRate const sampleRate;
  Phase phase{0.f};

  Phase operator()() noexcept {
    auto const prev = phase;
    phase = Phase(
      std::fmod(float(phase) + float(frequency) / float(sampleRate), 1.f));
    return prev;
  }
}; // struct Oscillator

class Sine {
public:
  float operator()(Oscillator& osc) noexcept {
    return std::sin(float(osc()) * kPI<float> * 2.f);
  }
}; // class Sine

class Saw {
public:
  constexpr Saw(int nH = 0) noexcept
    : numHarmonics_(nH) {}

  float operator()(Oscillator& osc) noexcept {
    if (numHarmonics_ == 0) {
      float frequency = float(osc.frequency);
      while (frequency < float(osc.sampleRate)) {
        numHarmonics_++;
        frequency *= 2.f;
      }
    }

    float const phase = float(osc()) * kPI<float> * 2.f;
    float ret = 0.f;

    for (int i = 1; i <= numHarmonics_; ++i) {
      ret += std::sin(phase * float(i)) / float(i);
    }

    return ret * 2.f / kPI<float>;
  }

private:
  int numHarmonics_;
}; // class Saw

class Square {
public:
  constexpr Square(int nH = 0) noexcept
    : numHarmonics_(nH) {}

  float operator()(Oscillator& osc) noexcept {
    if (numHarmonics_ == 0) {
      float const halfSR = float(osc.sampleRate) * .5f;
      while (float(osc.frequency) * float(numHarmonics_ * 2 - 1) < halfSR) {
        numHarmonics_++;
      }
      numHarmonics_--;
    }

    float const phase = float(osc()) * kPI<float> * 2.f;
    float ret = 0.f;

    for (int i = 1; i <= numHarmonics_; ++i) {
      float const j = float(i * 2 - 1);
      ret += std::sin(phase * j) / j;
    }

    return ret * 4.f / kPI<float>;
  }

private:
  int numHarmonics_;
}; // class Square

class Triangle {
public:
  constexpr Triangle(int nH = 0) noexcept
    : numHarmonics_(nH) {}

  float operator()(Oscillator& osc) noexcept {
    if (numHarmonics_ == 0) {
      float const halfSR = float(osc.sampleRate) * .5f;
      while (float(osc.frequency) * float(numHarmonics_ * 2 - 1) < halfSR) {
        numHarmonics_++;
      }
      numHarmonics_--;
    }

    float const phase = float(osc()) * kPI<float> * 2.f;
    bool subtract = true;
    float ret = 0.f;

    for (int i = 1; i <= numHarmonics_; ++i) {
      float const j = float(i * 2 - 1);
      ret += (subtract ? -1.f : 1.f) * std::sin(phase * j) / (j * j);
      subtract = !subtract;
    }

    return ret * 8.f / (kPI<float> * kPI<float>);
  }

private:
  int numHarmonics_;
}; // class Triangle

class Note {
public:
  enum class WaveForms { kSine, kSaw, kSquare, kTriangle };

  constexpr Note(Frequency f, WaveForms wF, SampleRate sR,
                 std::int32_t nH = 0) noexcept
    : frequency_(f)
    , waveForm_(wF)
    , sampleRate_(sR)
    , numHarmonics_(nH) {
    if (numHarmonics_ == 0) {
      switch (waveForm_) {
      case WaveForms::kSine: break;
      case WaveForms::kSaw: {
        float frequency = float(frequency_);
        while (frequency < float(sampleRate_)) {
          numHarmonics_++;
          frequency *= 2.f;
        }
      } break;
      case WaveForms::kSquare: {
        float const halfSR = float(sampleRate_) * .5f;
        while (float(frequency_) * float(numHarmonics_ * 2 - 1) < halfSR) {
          numHarmonics_++;
        }
        numHarmonics_--;
      } break;
      case WaveForms::kTriangle: {
        float const halfSR = float(sampleRate_) * .5f;
        while (float(frequency_) * float(numHarmonics_ * 2 - 1) < halfSR) {
          numHarmonics_++;
        }
        numHarmonics_--;
      } break;
      }
    }
  }

  Amplitude sample() noexcept {
    Seconds const age(float(currAge_.count()) / float(sampleRate_));
    currAge_++;
    Amplitude const env = calcEnv(age);
    Phase const phase = Phase(std::fmod(age.count() * float(frequency_), 1.f));
    return calcWF(phase) * env;
  }

  bool released{false};
  bool isDead() const noexcept { return dead_; }

  private:
    Frequency frequency_;
    WaveForms waveForm_;
    SampleRate sampleRate_;
    std::int32_t numHarmonics_;
    Ticks currAge_{0};
    Ticks releasedAge_{0};
    bool dead_{false};
    Seconds attackTime_{.2f};
    Amplitude attackAmplitude_{1.f};
    Seconds decayTime_{.5f};
    Amplitude decayAmplitude_{.5f};
    Seconds releaseTime_{.2f};

    static constexpr Amplitude Lerp(Amplitude a, Amplitude b, float t) {
      return Amplitude(float(b - a) * t + float(a));
    }

    Amplitude calcEnv(std::chrono::duration<float> time) noexcept {
      if (releasedAge_ == Ticks(0)) {
        if (released) {
          releasedAge_ = currAge_;
        } else {
          if (time < attackTime_) {
            return Amplitude(0.f);
          } else {
            return Lerp(Amplitude(0.f), attackAmplitude_,
                        (time - attackTime_) / (decayTime_ - attackTime_));
          }
        }
      }

      if (releasedAge_ != Ticks(0)) {
        if (time - Seconds(float(releasedAge_.count()) / float(sampleRate_)) >
            releaseTime_)
          dead_ = true;
      }

      if (time > releaseTime_) {
        return Amplitude(0.f);
      } else {
        return Lerp(decayAmplitude_, Amplitude(0.f),
                    (time - decayTime_) / (releaseTime_ - decayTime_));
      }
    } // calcEnv

    Amplitude calcWF(Phase phase) noexcept {
      float const fPhase = float(phase) * 2.f * kPI<float>;

      switch (waveForm_) {
      case WaveForms::kSine: return Amplitude(std::sin(fPhase));
      case WaveForms::kSaw: {
        float ret = 0.f;
        for (int i = 1; i <= numHarmonics_; ++i) {
          ret += std::sin(fPhase * float(i)) / float(i);
        }
        return Amplitude(ret * 2.f / kPI<float>);
      }
      case WaveForms::kSquare: {
        float ret = 0.f;
        for (int i = 1; i <= numHarmonics_; ++i) {
          float const j = float(i * 2 - 1);
          ret += std::sin(fPhase * j) / j;
        }
        return Amplitude(ret * 4.f / kPI<float>);
      }
      case WaveForms::kTriangle: {
        bool subtract = true;
        float ret = 0.f;
        for (int i = 1; i <= numHarmonics_; ++i) {
          float const j = float(i * 2 - 1);
          ret += (subtract ? -1.f : 1.f) * std::sin(fPhase * j) / (j * j);
          subtract = !subtract;
        }
        return Amplitude(ret * 8.f / (kPI<float> * kPI<float>));
      }
      default: return Amplitude(0.f);
      }
    } // calcWF
  };  // class Note

} // namespace synth

struct AudioDataNotes {
  int const outputChannelCount{2};
  synth::SampleRate const sampleRate{44100.f};

  std::vector<synth::Note> notes;
}; // struct AudioDataNotes

static int CallbackNotes(void const* inputBuffer [[maybe_unused]],
                         void* outputBuffer, unsigned long framesPerBuffer,
                         PaStreamCallbackTimeInfo const* timeInfo
                         [[maybe_unused]],
                         PaStreamCallbackFlags statusFlags [[maybe_unused]],
                         void* userData) noexcept {
  auto data = reinterpret_cast<AudioDataNotes*>(userData);
  int const numChannels = data->outputChannelCount;
  auto&& notes = data->notes;

  auto out = static_cast<float*>(outputBuffer);
  for (unsigned long i = 0; i < framesPerBuffer; ++i, out += numChannels) {
    synth::Amplitude value(0.f);
    for (auto&& note : notes) value += note.sample();
    for (int j = 0; j < numChannels; ++j) out[j] = float(value);
  }

  auto iter =
    std::remove_if(notes.begin(), notes.end(),
                   [](synth::Note const& note) { return note.isDead(); });
  if (iter != notes.end()) notes.erase(iter);

  return 0;
} // paCallbackNotes

#if 0
struct AudioData {
  int const outputChannelCount{2};
  synth::SampleRate const sampleRate{44100.f};

  synth::Oscillator osc{synth::Frequency(440.f), sampleRate};

  synth::Sine sine;
  synth::Square square;
  synth::Saw saw;
  synth::Triangle triangle;

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
                    PaStreamCallbackTimeInfo const* timeInfo [[maybe_unused]],
                    PaStreamCallbackFlags statusFlags [[maybe_unused]],
                    void* userData) noexcept {
  auto audioData = reinterpret_cast<AudioData*>(userData);

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
#endif

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
  control.mutable_window()->set_width(1000);
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

  AudioDataNotes audioData;
  PaStream* stream;

  err = Pa_OpenDefaultStream(
    &stream,
    0,                            // input channels
    audioData.outputChannelCount, // output channels
    paFloat32,                    // output format
    float(audioData.sampleRate),  // sample rate
    paFramesPerBufferUnspecified, // frames per buffer: let PA pick
    CallbackNotes,                // callback function
    &audioData                    // user data
  );

  if (err != paNoError) {
    logger.error("error opening default stream: {}", Pa_GetErrorText(err));
    std::exit(EXIT_FAILURE);
  }

  err = Pa_StartStream(stream);
  if (err != paNoError) {
    logger.error("error starting stream: {}", Pa_GetErrorText(err));
    std::exit(EXIT_FAILURE);
  }

  while (iris::Renderer::IsRunning()) {
    iris::Renderer::BeginFrame();

    if (ImGui::IsKeyPressed(iris::wsi::Keys::kA, false)) {
      audioData.notes.emplace_back(synth::Frequency(440.f),
                                   synth::Note::WaveForms::kSine,
                                   audioData.sampleRate);
    }
    if (ImGui::IsKeyReleased(iris::wsi::Keys::kA)) {
      audioData.notes[0].released = true;
    }

    if (ImGui::Begin("Synth")) {
      ImGui::BeginGroup();
      ImGui::TextColored(ImVec4(.4f, .2f, 1.f, 1.f), "Stream");
      PaStreamInfo const* streamInfo = Pa_GetStreamInfo(stream);
      ImGui::LabelText("Sample Rate", "%.3f", streamInfo->sampleRate);
      ImGui::LabelText("Input Latency", "%.3f", streamInfo->inputLatency);
      ImGui::LabelText("Output Latency", "%.3f", streamInfo->outputLatency);
      ImGui::LabelText("Number of Notes", "%ld", audioData.notes.size());
      char loadOverlay[16];
      std::snprintf(loadOverlay, 16, "CPU Load: %.2f",
                    Pa_GetStreamCpuLoad(stream));
      ImGui::ProgressBar((float)Pa_GetStreamCpuLoad(stream), {0.f, 0.f},
                         loadOverlay);
      ImGui::EndGroup();

      ImGui::BeginGroup();
      ImGui::TextColored(ImVec4(.4f, .2f, 1.f, 1.f), "Controls");

#if 0
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
#endif

      ImGui::EndGroup();
    }
    ImGui::End(); // Synth

    iris::Renderer::EndFrame();
  }

  err = Pa_StopStream(stream);
  if (err != paNoError) {
    logger.error("error stopping stream: {}", Pa_GetErrorText(err));
    std::exit(EXIT_FAILURE);
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
