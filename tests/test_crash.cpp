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

// Real process death during publication.
//
// A child process publishes generations forever; the parent terminates that
// process at a point the test does not control. What is asserted afterwards is
// the atomicity property, which does not depend on where the kill landed: after
// any number of killed publications the store must resolve to exactly one whole
// generation, never a hybrid, and it must accept a new publication afterwards.
//
// The deterministic sabotage of each individual persistence stage is covered by
// test_corruption.cpp; this file proves the same property against a genuine
// operating-system process termination.

#include <filesystem>
#include <string>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#endif

namespace {

using namespace cooling_capacity_accounting;

/// Starts a child that publishes forever, waits a short, fixed amount of work,
/// then terminates it. Returns true when the child was started.
[[nodiscard]] bool kill_publishing_child(const std::filesystem::path& root,
                                         std::size_t contributions) {
  const std::filesystem::path executable = cca_test::test_executable();
  const std::string root_text = root.string();
  const std::string count = std::to_string(contributions);
#ifdef _WIN32
  std::wstring command_line = L'"' + executable.wstring() + L'"';
  command_line += L" --child publish-forever ";
  command_line += L'"' + std::wstring(root_text.begin(), root_text.end()) + L'"';
  command_line += L' ';
  command_line += std::wstring(count.begin(), count.end());
  std::vector<wchar_t> mutable_line(command_line.begin(), command_line.end());
  mutable_line.push_back(L'\0');
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  if (CreateProcessW(executable.wstring().c_str(), mutable_line.data(), nullptr, nullptr,
                     FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                     &process) == FALSE) {
    return false;
  }
  // A short, fixed delay schedules the termination somewhere inside the
  // publish loop. No assertion depends on where that is.
  Sleep(120);
  static_cast<void>(TerminateProcess(process.hProcess, 7));
  static_cast<void>(WaitForSingleObject(process.hProcess, INFINITE));
  DWORD exit_code = 0;
  static_cast<void>(GetExitCodeProcess(process.hProcess, &exit_code));
  static_cast<void>(CloseHandle(process.hThread));
  static_cast<void>(CloseHandle(process.hProcess));
  return exit_code != 0;
#else
  const pid_t child = ::fork();
  if (child < 0) {
    return false;
  }
  if (child == 0) {
    ::execl(executable.c_str(), executable.c_str(), "--child", "publish-forever",
            root_text.c_str(), count.c_str(), static_cast<char*>(nullptr));
    ::_exit(120);
  }
  ::usleep(120000);
  static_cast<void>(::kill(child, SIGKILL));
  int status = 0;
  static_cast<void>(::waitpid(child, &status, 0));
  return true;
#endif
}

/// Asserts the store resolves to exactly one whole generation.
void assert_single_whole_generation(const std::filesystem::path& root,
                                    const char* label) {
  StoreOptions options;
  options.root = root;
  options.create_if_missing = true;
  Result<AccountingStore> store = AccountingStore::open(options);
  if (!store.ok()) {
    cca_test::note(std::string(label) + ": open failed: " + store.error().to_string());
  }
  CCA_CHECK(store.ok());
  if (!store.ok()) {
    return;
  }
  const Result<std::vector<StoredGeneration>> history = store.value().history();
  CCA_CHECK(history.ok());
  if (!history.ok()) {
    return;
  }
  std::size_t verified = 0;
  for (const StoredGeneration& record : history.value()) {
    const Result<AccountingSnapshot> snapshot = store.value().load(record.generation);
    CCA_CHECK(snapshot.ok());
    if (snapshot.ok()) {
      CCA_CHECK(snapshot.value().verify().ok());
      CCA_CHECK(snapshot.value().ledger().verify_closure().exact);
      verified += 1;
    }
  }
  CCA_CHECK_EQ(verified, history.value().size());
  if (store.value().has_generation()) {
    const Result<AccountingSnapshot> latest = store.value().latest();
    CCA_CHECK(latest.ok());
    if (latest.ok()) {
      CCA_CHECK(latest.value().verify().ok());
    }
  }
  CCA_CHECK(store.value().close().ok());
}

}  // namespace

CCA_TEST(crash_process_death_during_publication_leaves_one_whole_generation) {
  cca_test::TempDir workspace("crash-publish");
  for (int attempt = 0; attempt < 3; ++attempt) {
    CCA_CHECK(kill_publishing_child(workspace.path(), 400U));
    assert_single_whole_generation(workspace.path(), "crash during publication");
  }

  // The store is still usable after being killed mid-publication.
  StoreOptions options;
  options.root = workspace.path();
  options.create_if_missing = true;
  CCA_ASSIGN(store, AccountingStore::open(options));
  const AccountGeneration before = store.current_generation();
  const AccountingInput input = cca_test::make_facility(cca_test::FacilitySpec{});
  CCA_ASSIGN(snapshot, AccountingSnapshot::create(input, cca_test::fixture_now()));
  PublishRequest request;
  request.attempt = cca_test::attempt_of("22222222222222222222222222222222");
  request.fingerprint = snapshot.content_digest();
  request.expected_previous = before;
  request.expected_revision = snapshot.header().generations.revision;
  request.epoch = store.fence().epoch;
  request.published_at = cca_test::fixture_now();
  CCA_ASSIGN(outcome, store.publish(snapshot, request));
  CCA_CHECK(outcome.record.generation >= before);
  CCA_CHECK(store.close().ok());
  assert_single_whole_generation(workspace.path(), "after recovery publication");
}
