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

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace cca_test {
namespace {

std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

struct RunState {
  std::string current;
  std::size_t failures = 0;
};

RunState& state() {
  static RunState run_state;
  return run_state;
}

std::uint64_t allocation_counter = 0;

}  // namespace

void register_test(std::string name, std::function<void()> body) {
  registry().push_back(TestCase{std::move(name), std::move(body)});
}

void fail(const char* file, int line, std::string message) {
  state().failures += 1;
  std::cout << "  FAIL " << state().current << "\n    " << file << ":" << line << ": "
            << message << "\n";
}

void note(std::string message) {
  std::cout << "  note " << state().current << ": " << message << "\n";
}

std::string describe_error(const cooling_capacity_accounting::Error& error) {
  return std::string(cooling_capacity_accounting::error_code_name(error.code())) + ": " +
         error.to_string();
}

int run_all(const std::string& filter, bool list_only) {
  std::vector<TestCase*> selected;
  for (TestCase& test : registry()) {
    if (!filter.empty() && test.name.find(filter) == std::string::npos) {
      continue;
    }
    selected.push_back(&test);
  }
  std::sort(selected.begin(), selected.end(),
            [](const TestCase* lhs, const TestCase* rhs) { return lhs->name < rhs->name; });
  if (list_only) {
    for (const TestCase* test : selected) {
      std::cout << test->name << "\n";
    }
    return 0;
  }
  std::size_t failed = 0;
  const auto started = std::chrono::steady_clock::now();
  for (TestCase* test : selected) {
    state().current = test->name;
    state().failures = 0;
    const auto test_started = std::chrono::steady_clock::now();
    test->body();
    const auto test_finished = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                test_finished - test_started)
                                .count();
    if (state().failures == 0) {
      std::cout << "PASS " << test->name << " (" << elapsed_ms << " ms)\n";
    } else {
      failed += 1;
      std::cout << "FAIL " << test->name << " (" << state().failures << " failures, "
                << elapsed_ms << " ms)\n";
    }
  }
  const auto finished = std::chrono::steady_clock::now();
  const auto total_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count();
  std::cout << (failed == 0 ? "SUCCESS " : "FAILURE ") << selected.size()
            << " tests, " << failed << " failed, " << total_ms << " ms\n";
  return static_cast<int>(failed);
}

std::string read_text_file(const std::filesystem::path& path) {
  std::error_code code;
  if (!std::filesystem::exists(path, code)) {
    return std::string();
  }
#ifdef _WIN32
  std::FILE* file = nullptr;
  if (_wfopen_s(&file, path.wstring().c_str(), L"rb") != 0 || file == nullptr) {
    return std::string();
  }
#else
  std::FILE* file = std::fopen(path.string().c_str(), "rb");
  if (file == nullptr) {
    return std::string();
  }
#endif
  std::string text;
  char buffer[4096];
  std::size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    text.append(buffer, read);
  }
  static_cast<void>(std::fclose(file));
  return text;
}

void write_text_file(const std::filesystem::path& path, std::string_view text) {
  std::error_code code;
  static_cast<void>(std::filesystem::remove(path, code));
#ifdef _WIN32
  std::FILE* file = nullptr;
  if (_wfopen_s(&file, path.wstring().c_str(), L"wb") != 0 || file == nullptr) {
    return;
  }
#else
  std::FILE* file = std::fopen(path.string().c_str(), "wb");
  if (file == nullptr) {
    return;
  }
#endif
  if (!text.empty()) {
    static_cast<void>(std::fwrite(text.data(), 1, text.size(), file));
  }
  static_cast<void>(std::fclose(file));
}

std::string report_field(std::string_view report, std::string_view key) {
  std::string needle(key);
  needle.push_back('=');
  std::size_t position = report.find(needle);
  if (position == std::string_view::npos) {
    return std::string();
  }
  position += needle.size();
  const std::size_t end = report.find_first_of(" \n\r\t", position);
  return std::string(report.substr(position, end == std::string_view::npos
                                                 ? std::string_view::npos
                                                 : end - position));
}

TempDir::TempDir(std::string_view label) {
  const std::uint64_t counter = ++allocation_counter;
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  std::error_code code;
  std::filesystem::path base = std::filesystem::temp_directory_path(code);
  if (code) {
    base = std::filesystem::current_path();
  }
  // The counter and the clock make the name unique per process run; the label
  // makes a leftover directory identifiable.
  path_ = base / ("cca-test-" + std::string(label) + "-" +
                  std::to_string(static_cast<unsigned long long>(counter)) + "-" +
                  std::to_string(static_cast<long long>(now)));
  std::error_code create_code;
  static_cast<void>(std::filesystem::create_directories(path_, create_code));
  if (create_code) {
    std::cout << "  NOTE " << label << ": could not create " << path_.string() << ": "
              << create_code.message() << "\n";
  }
}

TempDir::~TempDir() {
  std::error_code code;
  static_cast<void>(std::filesystem::remove_all(path_, code));
  if (code) {
    std::cout << "  NOTE leftover temporary directory " << path_.string() << ": "
              << code.message() << "\n";
  }
}

void TempDir::remove_now() {
  std::error_code code;
  static_cast<void>(std::filesystem::remove_all(path_, code));
}

}  // namespace cca_test
