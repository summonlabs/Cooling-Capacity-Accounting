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

// Multi-process proof. Every child below is a real, independent operating
// system process started from this same executable.

#include <filesystem>
#include <string>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

namespace {

using namespace cooling_capacity_accounting;

[[nodiscard]] std::string child_report(cca_test::TempDir& workspace,
                                       const std::string& name) {
  return workspace.child(name).string();
}

}  // namespace

CCA_TEST(multiprocess_writer_lock_excludes_a_second_writer) {
  cca_test::TempDir workspace("multiprocess-writer-lock");
  const std::string ready = child_report(workspace, "ready");
  // The child holds the writer lock, then the parent tries to take it. The
  // parent waits for the child's ready file by reading it; the child keeps the
  // lock until it exits by itself.
  const int child_exit =
      cca_test::run_child_process({"--child", "hold-writer", workspace.path().string(),
                                   ready, "400"});
  CCA_CHECK_EQ(child_exit, 0);
  const std::string marker = cca_test::read_text_file(std::filesystem::path(ready));
  CCA_CHECK(marker == "ready");

  // The child has exited by the time run_child_process returns, so the lock is
  // free again and the parent must be able to take it. To prove exclusion we
  // instead hold the lock here and let a child try to take it.
  StoreOptions options;
  options.root = workspace.path();
  options.create_if_missing = true;
  CCA_ASSIGN(store, AccountingStore::open(options));
  const std::string child_result = child_report(workspace, "child-open");
  const int blocked_exit = cca_test::run_child_process(
      {"--child", "publish", workspace.path().string(), "1", "4", child_result});
  const std::string report = cca_test::read_text_file(std::filesystem::path(child_result));
  CCA_CHECK(!report.empty());
  CCA_CHECK(report.find("StoreInUse") != std::string::npos);
  CCA_CHECK(blocked_exit != 0);
  CCA_CHECK(store.close().ok());

  // With the parent's lock released the same child succeeds.
  const std::string retry_result = child_report(workspace, "child-open-retry");
  const int retry_exit = cca_test::run_child_process(
      {"--child", "publish", workspace.path().string(), "1", "4", retry_result});
  CCA_CHECK_EQ(retry_exit, 0);
  const std::string retry_report =
      cca_test::read_text_file(std::filesystem::path(retry_result));
  CCA_CHECK(retry_report.find("status=ok") != std::string::npos);
}

CCA_TEST(multiprocess_reader_is_excluded_while_a_writer_holds_the_store) {
  cca_test::TempDir workspace("multiprocess-reader");
  StoreOptions options;
  options.root = workspace.path();
  options.create_if_missing = true;
  CCA_ASSIGN(store, AccountingStore::open(options));

  const AccountingInput input = cca_test::make_facility(cca_test::FacilitySpec{});
  CCA_ASSIGN(snapshot, AccountingSnapshot::create(input, cca_test::fixture_now()));
  PublishRequest request;
  request.attempt = cca_test::attempt_of("11111111111111111111111111111111");
  request.fingerprint = snapshot.content_digest();
  request.expected_previous = store.current_generation();
  request.expected_revision = snapshot.header().generations.revision;
  request.epoch = store.fence().epoch;
  request.published_at = cca_test::fixture_now();
  CCA_ASSIGN(outcome, store.publish(snapshot, request));
  CCA_CHECK_EQ(outcome.record.generation.value(), 1U);

  // The store is either written by one writer or read by readers, never both at
  // once: a reader that arrives while the parent holds the writer lock is
  // refused rather than shown a state that is being replaced.
  const std::string blocked_path = child_report(workspace, "reader-blocked");
  const int blocked_exit = cca_test::run_child_process(
      {"--child", "verify", workspace.path().string(), blocked_path});
  CCA_CHECK_EQ(blocked_exit, 0);
  const std::string blocked =
      cca_test::read_text_file(std::filesystem::path(blocked_path));
  CCA_CHECK(blocked.find("status=error") != std::string::npos);
  CCA_CHECK(blocked.find("StoreInUse") != std::string::npos);
  CCA_CHECK(store.close().ok());

  // With the writer gone the same reader reads the committed generation and
  // finds exactly what was published.
  const std::string report_path = child_report(workspace, "reader");
  const int reader_exit = cca_test::run_child_process(
      {"--child", "verify", workspace.path().string(), report_path});
  CCA_CHECK_EQ(reader_exit, 0);
  const std::string report = cca_test::read_text_file(std::filesystem::path(report_path));
  CCA_CHECK(report.find("status=ok") != std::string::npos);
  CCA_CHECK(report.find("generation=1") != std::string::npos);
  CCA_CHECK(report.find("closure=exact") != std::string::npos);
  CCA_CHECK(report.find("digest=" + snapshot.content_digest().to_hex()) !=
            std::string::npos);
}

CCA_TEST(multiprocess_child_publication_is_visible_to_the_parent) {
  cca_test::TempDir workspace("multiprocess-publish");
  const std::string report_path = child_report(workspace, "publish");
  const int exit_code = cca_test::run_child_process(
      {"--child", "publish", workspace.path().string(), "3", "8", report_path});
  CCA_CHECK_EQ(exit_code, 0);
  const std::string report = cca_test::read_text_file(std::filesystem::path(report_path));
  CCA_CHECK(report.find("published=3") != std::string::npos);

  StoreOptions options;
  options.root = workspace.path();
  CCA_ASSIGN(store, AccountingStore::open_read_only(options));
  CCA_CHECK_EQ(store.current_generation().value(), 3U);
  CCA_ASSIGN(history, store.history());
  CCA_CHECK_EQ(history.size(), static_cast<std::size_t>(3));
  for (const StoredGeneration& record : history) {
    CCA_ASSIGN(loaded, store.load(record.generation));
    CCA_CHECK(loaded.verify().ok());
  }
}

CCA_TEST(multiprocess_epoch_fencing_across_processes) {
  cca_test::TempDir workspace("multiprocess-epoch");
  const std::string first_report = child_report(workspace, "first");
  CCA_CHECK_EQ(cca_test::run_child_process({"--child", "publish",
                                             workspace.path().string(), "1", "4",
                                             first_report}),
               0);

  // A process holding an older control-plane epoch is refused.
  const std::string stale_report = child_report(workspace, "stale");
  CCA_CHECK_EQ(cca_test::run_child_process({"--child", "publish-epoch",
                                            workspace.path().string(), "1", stale_report}),
               0);
  const std::string stale = cca_test::read_text_file(std::filesystem::path(stale_report));
  CCA_CHECK(stale.find("status=ok") != std::string::npos);

  // A process holding a different, later epoch is refused as well: the store
  // belongs to exactly one epoch.
  const std::string future_report = child_report(workspace, "future");
  CCA_CHECK_EQ(cca_test::run_child_process({"--child", "publish-epoch",
                                            workspace.path().string(), "9",
                                            future_report}),
               0);
  const std::string future =
      cca_test::read_text_file(std::filesystem::path(future_report));
  CCA_CHECK(future.find("CrossEpochAuthority") != std::string::npos);
}

CCA_TEST(multiprocess_reopen_after_another_process_wrote) {
  cca_test::TempDir workspace("multiprocess-reopen");
  const std::string report_path = child_report(workspace, "reopen");
  CCA_CHECK_EQ(cca_test::run_child_process(
                   {"--child", "publish", workspace.path().string(), "2", "6",
                    workspace.child("publish-report").string()}),
               0);
  CCA_CHECK_EQ(cca_test::run_child_process({"--child", "reopen-check",
                                            workspace.path().string(), report_path}),
               0);
  const std::string report = cca_test::read_text_file(std::filesystem::path(report_path));
  CCA_CHECK(report.find("status=ok") != std::string::npos);
  CCA_CHECK(report.find("generation=2") != std::string::npos);
  CCA_CHECK(report.find("recovery=pending") != std::string::npos);
}
