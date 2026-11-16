// Microphone capture.
//
// waveIn (winmm) in shared mode at 16 kHz mono 16-bit: the Windows audio
// mapper does the resampling and downmixing, so there is no resampler and no
// COM in this file. Samples are converted to float32 and handed to a sink.
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <vector>

namespace pt {
namespace win32 {

class AudioCapture {
 public:
  // Called from the audio callback thread with mono float32 samples at 16 kHz.
  using Sink = std::function<void(const float* samples, size_t count)>;

  ~AudioCapture();

  // `device_hint` is a substring of the device name ("" = the system default).
  bool start(Sink sink, const std::string& device_hint, std::string& err);

  // The names of the capture devices waveIn can see.
  static std::vector<std::string> device_names();
  void stop();
  bool is_running() const { return running_; }

 private:
  static void CALLBACK callback(HWAVEIN wave_in, UINT message, UINT_PTR instance,
                                DWORD_PTR parameter, DWORD_PTR reserved);

  void deliver(const void* data, size_t bytes);

  HWAVEIN wave_in_ = nullptr;
  bool running_ = false;
  Sink sink_;
  std::vector<WAVEHDR*> headers_;

  // waveInReset must not run while the callback is inside waveInAddBuffer —
  // that deadlocks the driver. `in_add_buffer_` tracks the callback and
  // `stopping_` tells it to stop re-arming buffers.
  std::mutex driver_mutex_;
  std::condition_variable driver_cv_;
  bool stopping_ = false;
  bool in_add_buffer_ = false;
};

}  // namespace win32
}  // namespace pt
