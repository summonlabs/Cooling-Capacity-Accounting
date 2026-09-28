// Copyright 2026 Summon Software Labs
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "test_harness.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace cca_test {
namespace {

#ifdef _WIN32

/// Quotes one argument for the Windows command line, following the rules the
/// C runtime uses when it re-parses argv.
[[nodiscard]] std::wstring quote_argument(const std::string& argument) {
  const std::wstring wide(argument.begin(), argument.end());
  const bool needs_quotes =
      argument.empty() || argument.find(' ') != std::string::npos ||
      argument.find('\t') != std::string::npos || argument.find('"') != std::string::npos;
  if (!needs_quotes) {
    return wide;
  }
  std::wstring quoted;
  quoted.push_back(L'"');
  std::size_t backslashes = 0;
  for (const wchar_t character : wide) {
    if (character == L'\\') {
      backslashes += 1;
      continue;
    }
    if (character == L'"') {
      quoted.append(backslashes * 2U + 1U, L'\\');
      quoted.push_back(L'"');
      backslashes = 0;
      continue;
    }
    quoted.append(backslashes, L'\\');
    backslashes = 0;
    quoted.push_back(character);
  }
  quoted.append(backslashes * 2U, L'\\');
  quoted.push_back(L'"');
  return quoted;
}

#endif

}  // namespace

const std::filesystem::path& test_executable() {
  static const std::filesystem::path path = []() -> std::filesystem::path {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer);
#else
    std::error_code code;
    std::filesystem::path candidate =
        std::filesystem::read_symlink("/proc/self/exe", code);
    if (!code && !candidate.empty()) {
      return candidate;
    }
    return std::filesystem::current_path() / std::filesystem::path("cca_tests");
#endif
  }();
  return path;
}

int run_child_process(const std::vector<std::string>& arguments) {
#ifdef _WIN32
  std::wstring command_line = quote_argument(test_executable().string());
  for (const std::string& argument : arguments) {
    command_line.push_back(L' ');
    command_line += quote_argument(argument);
  }
  std::vector<wchar_t> mutable_line(command_line.begin(), command_line.end());
  mutable_line.push_back(L'\0');

  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  // The child is independent: it inherits no handle, opens no console of its
  // own, and its stdio goes to the test runner's console.
  const BOOL created =
      CreateProcessW(test_executable().wstring().c_str(), mutable_line.data(), nullptr,
                     nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                     &process);
  if (created == FALSE) {
    return -1;
  }
  static_cast<void>(WaitForSingleObject(process.hProcess, INFINITE));
  DWORD exit_code = 0;
  static_cast<void>(GetExitCodeProcess(process.hProcess, &exit_code));
  static_cast<void>(CloseHandle(process.hThread));
  static_cast<void>(CloseHandle(process.hProcess));
  return static_cast<int>(exit_code);
#else
  const std::string executable = test_executable().string();
  std::vector<std::string> storage;
  storage.push_back(executable);
  for (const std::string& argument : arguments) {
    storage.push_back(argument);
  }
  std::vector<char*> argv;
  for (std::string& entry : storage) {
    argv.push_back(entry.data());
  }
  argv.push_back(nullptr);
  const pid_t child = ::fork();
  if (child < 0) {
    return -1;
  }
  if (child == 0) {
    ::execv(executable.c_str(), argv.data());
    ::_exit(120);
  }
  int status = 0;
  if (::waitpid(child, &status, 0) < 0) {
    return -1;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return 128 + WTERMSIG(status);
#endif
}

}  // namespace cca_test
