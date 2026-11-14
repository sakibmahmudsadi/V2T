#include "platform/win32/audio.h"

#include <mmsystem.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace pt {
namespace win32 {
namespace {

// MinGW's headers do not declare the waveIn callback typedef, so we do.
typedef void (CALLBACK* WaveInProc)(HWAVEIN hwi, UINT message, UINT_PTR instance,
                                    DWORD_PTR param1, DWORD_PTR param2);

constexpr int kSampleRate = 16000;
constexpr int kChannels = 1;
constexpr int kBitsPerSample = 16;
constexpr int kBufferCount = 4;
constexpr int kBufferMillis = 250;

}  // namespace

AudioCapture::~AudioCapture() {
  stop();
}

bool AudioCapture::start(Sink sink, std::string& err) {
  if (running_) return true;
  sink_ = std::move(sink);
  {
    std::lock_guard<std::mutex> lock(driver_mutex_);
    stopping_ = false;
    in_add_buffer_ = false;
  }

  WAVEFORMATEX format;
  std::memset(&format, 0, sizeof(format));
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = static_cast<WORD>(kChannels);
  format.nSamplesPerSec = static_cast<DWORD>(kSampleRate);
  format.wBitsPerSample = static_cast<WORD>(kBitsPerSample);
  format.nBlockAlign = static_cast<WORD>(kChannels * kBitsPerSample / 8);
  format.nAvgBytesPerSec = static_cast<DWORD>(kSampleRate * format.nBlockAlign);

  HWAVEIN device = nullptr;
  const MMRESULT opened = waveInOpen(
      &device, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(&AudioCapture::callback),
      reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
  if (opened != MMSYSERR_NOERROR || device == nullptr) {
    err = "cannot open the microphone (no input device, or it is in use exclusively)";
    return false;
  }
  wave_in_ = device;

  const int buffer_bytes = kSampleRate * kBufferMillis / 1000 * format.nBlockAlign;
  for (int i = 0; i < kBufferCount; ++i) {
    auto* header = new WAVEHDR();
    std::memset(header, 0, sizeof(WAVEHDR));
    header->lpData = static_cast<LPSTR>(std::malloc(static_cast<size_t>(buffer_bytes)));
    header->dwBufferLength = static_cast<DWORD>(buffer_bytes);
    if (header->lpData == nullptr ||
        waveInPrepareHeader(device, header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
      std::free(header->lpData);
      delete header;
      stop();
      err = "cannot prepare a microphone buffer";
      return false;
    }
    headers_.push_back(header);
    if (waveInAddBuffer(device, header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
      stop();
      err = "cannot queue a microphone buffer";
      return false;
    }
  }

  if (waveInStart(device) != MMSYSERR_NOERROR) {
    stop();
    err = "cannot start microphone capture";
    return false;
  }

  running_ = true;
  return true;
}

void AudioCapture::stop() {
  if (wave_in_ == nullptr) {
    headers_.clear();
    return;
  }
  const HWAVEIN device = wave_in_;

  // Stop the device first, then wait until no callback is inside
  // waveInAddBuffer: waveInReset deadlocks if it runs concurrently with it.
  {
    std::unique_lock<std::mutex> lock(driver_mutex_);
    stopping_ = true;
    while (in_add_buffer_) driver_cv_.wait(lock);
  }

  waveInStop(device);
  waveInReset(device);
  for (WAVEHDR* header : headers_) {
    waveInUnprepareHeader(device, header, sizeof(WAVEHDR));
    std::free(header->lpData);
    delete header;
  }
  headers_.clear();
  waveInClose(device);
  wave_in_ = nullptr;
  running_ = false;
}

void CALLBACK AudioCapture::callback(HWAVEIN wave_in, UINT message, UINT_PTR instance,
                                     DWORD_PTR parameter, DWORD_PTR reserved) {
  (void)reserved;
  if (message != WIM_DATA) return;

  auto* header = reinterpret_cast<WAVEHDR*>(parameter);
  auto* capture = reinterpret_cast<AudioCapture*>(static_cast<uintptr_t>(instance));
  if (header == nullptr || capture == nullptr) return;

  capture->deliver(header->lpData, header->dwBufferLength);

  // Hand the buffer back so the driver fills it again — unless the capture is
  // being torn down, in which case the device must not be touched.
  {
    std::unique_lock<std::mutex> lock(capture->driver_mutex_);
    if (capture->stopping_) return;
    capture->in_add_buffer_ = true;
  }
  waveInAddBuffer(wave_in, header, sizeof(WAVEHDR));
  {
    std::lock_guard<std::mutex> lock(capture->driver_mutex_);
    capture->in_add_buffer_ = false;
  }
  capture->driver_cv_.notify_all();
}

void AudioCapture::deliver(const void* data, size_t bytes) {
  if (!sink_ || data == nullptr) return;
  const size_t count = bytes / (kBitsPerSample / 8);
  if (count == 0) return;

  std::vector<float> samples(count);
  const int16_t* source = static_cast<const int16_t*>(data);
  for (size_t i = 0; i < count; ++i) {
    samples[i] = static_cast<float>(source[i]) / 32768.0f;
  }
  sink_(samples.data(), count);
}

}  // namespace win32
}  // namespace pt
