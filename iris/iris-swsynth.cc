#define _USE_MATH_DEFINES

#include "iris/config.h"

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/parse.h"
#include "gsl/gsl"
#include "portaudio.h"
#include "spdlog/logger.h"
#include "spdlog/sinks/ansicolor_sink.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include <cmath>

static std::shared_ptr<spdlog::logger> sLogger;

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
}; // struct AudioData

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
    float const sample = audioData->triangle(audioData->osc);
    *fltOutput++ = sample * .2f;
    *fltOutput++ = sample * .2f;
  }

  return 0;
} // paCallback

int main(int argc, char** argv) {
  absl::InitializeSymbolizer(argv[0]);
  absl::InstallFailureSignalHandler({});

  auto const positional = absl::ParseCommandLine(argc, argv);

  auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
    "iris-raytracer.log", true);
  file_sink->set_level(spdlog::level::trace);

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::trace);

  sLogger = std::shared_ptr<spdlog::logger>(
    new spdlog::logger("iris-viewer", {console_sink, file_sink}));
  sLogger->set_level(spdlog::level::trace);

  sLogger->info("Logging initialized");

  PaError err = Pa_Initialize();
  if (err != paNoError) {
    sLogger->error("error initializing audio: {}", Pa_GetErrorText(err));
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
    sLogger->error("error opening default stream: {}", Pa_GetErrorText(err));
    std::exit(EXIT_FAILURE);
  }

  err = Pa_StartStream(stream);
  if (err != paNoError) {
    sLogger->error("error starting stream: {}", Pa_GetErrorText(err));
    std::exit(EXIT_FAILURE);
  }

  Pa_Sleep(5 * 1000);

  err = Pa_StopStream(stream);
  if (err != paNoError) {
    sLogger->error("error stopping stream: {}", Pa_GetErrorText(err));
    std::exit(EXIT_FAILURE);
  }

  err = Pa_CloseStream(stream);
  if (err != paNoError) {
    sLogger->error("error closing stream: {}", Pa_GetErrorText(err));
    std::exit(EXIT_FAILURE);
  }

  err = Pa_Terminate();
  if (err != paNoError) {
    sLogger->error("error terminating audio: {}", Pa_GetErrorText(err));
  }
  sLogger->info("exiting");
}
