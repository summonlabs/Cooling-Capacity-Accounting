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

#ifndef COOLING_CAPACITY_ACCOUNTING_TESTS_TEST_HARNESS_HPP
#define COOLING_CAPACITY_ACCOUNTING_TESTS_TEST_HARNESS_HPP

// A deliberately small test harness: tests are proof obligations, so the
// harness records the file, line, expression and message of every failure, and
// the process exits non-zero when any obligation failed. There is no timeout
// mechanism anywhere in this suite.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "cooling_capacity_accounting/errors.hpp"

namespace cca_test {

using cooling_capacity_accounting::ErrorCode;
using cooling_capacity_accounting::Result;

struct TestCase {
  std::string name;
  std::function<void()> body;
};

/// Registers a test at static-initialisation time.
void register_test(std::string name, std::function<void()> body);

/// Records a failure of the currently running test.
void fail(const char* file, int line, std::string message);
/// Records an informational note printed with the test result.
void note(std::string message);

/// Runs every registered test, or the ones matching a filter.
/// Returns the number of failed tests.
int run_all(const std::string& filter, bool list_only);

/// A deterministic pseudo-random generator, so a failing randomised test can be
/// reproduced from its printed seed.
class SeededRandom {
 public:
  explicit SeededRandom(std::uint64_t seed) : state_(seed == 0 ? 0x9E3779B97F4A7C15ULL
                                                               : seed) {}

  [[nodiscard]] std::uint64_t next_u64() {
    state_ ^= state_ << 13U;
    state_ ^= state_ >> 7U;
    state_ ^= state_ << 17U;
    return state_;
  }
  [[nodiscard]] std::uint32_t next_u32() {
    return static_cast<std::uint32_t>(next_u64() >> 32U);
  }
  /// Uniform value in [low, high].
  [[nodiscard]] std::int64_t next_range(std::int64_t low, std::int64_t high) {
    if (high <= low) {
      return low;
    }
    const std::uint64_t span = static_cast<std::uint64_t>(high - low) + 1U;
    return low + static_cast<std::int64_t>(next_u64() % span);
  }
  [[nodiscard]] bool next_bool() { return (next_u64() & 1U) == 1U; }
  [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

 private:
  std::uint64_t state_;
  std::uint64_t seed_ = 0;
};

/// A unique temporary directory that removes itself, including on failure.
class TempDir {
 public:
  explicit TempDir(std::string_view label);
  ~TempDir();

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
  [[nodiscard]] std::filesystem::path child(std::string_view name) const {
    return path_ / std::filesystem::path(std::string(name));
  }
  /// Removes the directory and everything below it. Safe to call twice.
  void remove_now();

 private:
  std::filesystem::path path_;
};

/// The path of the running test executable, used to spawn real child processes.
[[nodiscard]] const std::filesystem::path& test_executable();

/// Runs the test executable again as a real independent operating-system
/// process with the given arguments. The child writes its own report file when
/// the mode asks for one. Returns the exit code, or -1 when the child could not
/// be started.
int run_child_process(const std::vector<std::string>& arguments);

/// Formats an error for a failure message.
[[nodiscard]] std::string describe_error(const cooling_capacity_accounting::Error& error);

/// Reads a whole text file, or returns an empty string when it does not exist.
[[nodiscard]] std::string read_text_file(const std::filesystem::path& path);
/// Writes a whole text file, truncating anything already there.
void write_text_file(const std::filesystem::path& path, std::string_view text);
/// Reads "key=value" out of a child process report, or returns an empty string.
[[nodiscard]] std::string report_field(std::string_view report, std::string_view key);

}  // namespace cca_test

#define CCA_TEST(name)                                                        \
  static void cca_test_body_##name();                                         \
  namespace {                                                                 \
  const bool cca_test_registered_##name = []() {                              \
    ::cca_test::register_test(#name, &cca_test_body_##name);                   \
    return true;                                                              \
  }();                                                                        \
  }                                                                           \
  static void cca_test_body_##name()

#define CCA_FAIL(message) ::cca_test::fail(__FILE__, __LINE__, (message))

#define CCA_CHECK(condition)                                                  \
  do {                                                                        \
    if (!(condition)) {                                                       \
      ::cca_test::fail(__FILE__, __LINE__, "check failed: " #condition);      \
    }                                                                         \
  } while (false)

#define CCA_CHECK_EQ(lhs, rhs)                                                \
  do {                                                                        \
    const auto& cca_lhs_ = (lhs);                                             \
    const auto& cca_rhs_ = (rhs);                                             \
    if (!(cca_lhs_ == cca_rhs_)) {                                            \
      ::cca_test::fail(__FILE__, __LINE__,                                    \
                       std::string("check failed: " #lhs " == " #rhs));       \
    }                                                                         \
  } while (false)

/// Asserts that a Result carries the expected error code, and reports the
/// unexpected code otherwise.
#define CCA_CHECK_CODE(expression, expected)                                  \
  do {                                                                        \
    const auto cca_result_ = (expression);                                    \
    if (cca_result_.ok()) {                                                   \
      ::cca_test::fail(__FILE__, __LINE__,                                    \
                       std::string("expected failure " #expected " from " #expression)); \
    } else if (cca_result_.error().code() != (expected)) {                    \
      ::cca_test::fail(__FILE__, __LINE__,                                    \
                       std::string("expected " #expected " but got "          \
                                   "unexpected code: ") +                     \
                           ::cca_test::describe_error(cca_result_.error()));  \
    }                                                                         \
  } while (false)

/// Binds the value of a successful Result, failing the test when it failed.
#define CCA_ASSIGN(name, expression)                                          \
  auto cca_value_##name = (expression);                                       \
  if (!cca_value_##name.ok()) {                                               \
    ::cca_test::fail(__FILE__, __LINE__,                                      \
                     std::string("unexpected failure from " #expression ": ") + \
                         ::cca_test::describe_error(cca_value_##name.error())); \
    return;                                                                   \
  }                                                                           \
  auto& name = cca_value_##name.value()

#define CCA_REQUIRE_OK(expression)                                            \
  do {                                                                        \
    const auto cca_required_ = (expression);                                  \
    if (!cca_required_.ok()) {                                                \
      ::cca_test::fail(__FILE__, __LINE__,                                    \
                       std::string("unexpected failure from " #expression ": ") + \
                           ::cca_test::describe_error(cca_required_.error())); \
      return;                                                                 \
    }                                                                         \
  } while (false)

#endif  // COOLING_CAPACITY_ACCOUNTING_TESTS_TEST_HARNESS_HPP
