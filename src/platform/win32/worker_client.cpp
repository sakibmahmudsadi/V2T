#include "platform/win32/worker_client.h"

#include <vector>

#include "core/ipc.h"

namespace pt {
namespace win32 {
namespace {

std::wstring widen(const std::string& value) {
  if (value.empty()) return std::wstring();
  const int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                          static_cast<int>(value.size()), nullptr, 0);
  std::wstring result(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0],
                      needed);
  return result;
}

}  // namespace

WorkerClient::~WorkerClient() {
  stop();
}

bool WorkerClient::start(const std::string& worker_path, const std::string& pipe_name,
                         const std::string& backend, const std::string& engine_dll,
                         std::string& err) {
  stop();

  const std::wstring full_pipe = std::wstring(L"\\\\.\\pipe\\") + widen(pipe_name);

  HANDLE pipe = CreateNamedPipeW(full_pipe.c_str(), PIPE_ACCESS_DUPLEX,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                 1, 64 * 1024, 64 * 1024, 0, nullptr);
  if (pipe == nullptr) {
    err = "cannot create the worker pipe (Windows error " + std::to_string(GetLastError()) +
          ")";
    return false;
  }

  // The worker reads this when it starts; it decides which compute backends
  // the engine may register, and that has to be decided before the engine DLL
  // is loaded.
  const std::string mask = backend == "vulkan" ? "cpu,vulkan" : "cpu";
  SetEnvironmentVariableA("TRANSCRIBE_BACKENDS", mask.c_str());
  SetEnvironmentVariableA("PARAKEETTYPE_PIPE", pipe_name.c_str());

  STARTUPINFOW startup;
  ZeroMemory(&startup, sizeof(startup));
  startup.cb = sizeof(startup);

  PROCESS_INFORMATION info;
  ZeroMemory(&info, sizeof(info));

  std::wstring command;
  command += L"\"";
  command += widen(worker_path);
  command += L"\" --serve --pipe \"";
  command += widen(pipe_name);
  command += L"\"";
  if (!engine_dll.empty()) {
    command += L" --engine \"";
    command += widen(engine_dll);
    command += L"\"";
  }
  std::vector<wchar_t> command_buffer(command.begin(), command.end());
  command_buffer.push_back(L'\0');

  if (!CreateProcessW(nullptr, command_buffer.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                      &startup, &info)) {
    CloseHandle(pipe);
    err = "cannot start the worker '" + worker_path + "' (Windows error " +
          std::to_string(GetLastError()) + ")";
    return false;
  }
  CloseHandle(info.hThread);
  process_ = info.hProcess;

  // Wait for the worker to connect (it is busy starting up).
  const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : FALSE;
  if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
    err = "the worker did not connect (Windows error " + std::to_string(GetLastError()) + ")";
    stop();
    return false;
  }

  pipe_ = pipe;
  return true;
}

bool WorkerClient::send_control(const std::string& json, std::string& err) {
  if (!ipc::valid(pipe_)) {
    err = "the worker is not connected";
    return false;
  }
  if (!ipc::write_frame(pipe_, ipc::kFrameControl, json.data(), json.size())) {
    err = "cannot send a message to the worker";
    return false;
  }
  return true;
}

bool WorkerClient::send_audio(const float* samples, size_t count, std::string& err) {
  if (!ipc::valid(pipe_)) {
    err = "the worker is not connected";
    return false;
  }
  if (!ipc::write_frame(pipe_, ipc::kFrameAudio, samples, count * sizeof(float))) {
    err = "cannot send audio to the worker";
    return false;
  }
  return true;
}

bool WorkerClient::read_control(std::string& json, std::string& err) {
  if (!ipc::valid(pipe_)) {
    err = "the worker is not connected";
    return false;
  }
  if (!ipc::read_control(pipe_, json)) {
    err = "the worker closed the connection";
    return false;
  }
  return true;
}

void WorkerClient::stop() {
  if (pipe_ != nullptr) {
    CloseHandle(pipe_);
    pipe_ = nullptr;
  }
  if (process_ != nullptr) {
    TerminateProcess(process_, 0);
    CloseHandle(process_);
    process_ = nullptr;
  }
}

}  // namespace win32
}  // namespace pt
