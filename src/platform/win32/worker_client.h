// Controller side of the worker process.
//
// The controller owns a short-lived worker: it spawns V2TWorker.exe,
// talks to it over a named pipe, and lets it exit when the model has been idle
// for the configured time. That keeps the always-on process free of the ~1 GB
// the model needs.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace pt {
namespace win32 {

class WorkerClient {
 public:
  ~WorkerClient();

  // Spawns the worker and connects the pipe. `backend` is "cpu" or "vulkan";
  // it is passed to the worker through TRANSCRIBE_BACKENDS so the GPU driver
  // stays out of a CPU-only run.
  bool start(const std::string& worker_path, const std::string& pipe_name,
             const std::string& backend, const std::string& engine_dll, std::string& err);

  // Sends a JSON control message.
  bool send_control(const std::string& json, std::string& err);

  // Sends raw float32 PCM.
  bool send_audio(const float* samples, size_t count, std::string& err);

  // Waits for the next control message (blocking).
  bool read_control(std::string& json, std::string& err);

  bool is_running() const { return process_ != nullptr && pipe_ != nullptr; }
  void stop();

 private:
  HANDLE process_ = nullptr;
  HANDLE pipe_ = nullptr;
};

}  // namespace win32
}  // namespace pt
