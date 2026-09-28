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

// Real process restarts: generations published by an independent operating
// system process, recovered by this one, and the cross-process writer fence.

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "fixtures.hpp"
#include "test_harness.hpp"

using namespace cooling_capacity_accounting;
using cca_test::TempDir;
using cca_test::fixture_now;
using cca_test::read_text_file;
using cca_test::report_field;
using cca_test::run_child_process;

namespace {

[[nodiscard]] std::string child_report(const std::filesystem::path& path) {
  const std::string text = read_text_file(path);
  CCA_CHECK(!text.empty());
  return text;
}

[[nodiscard]] AccountingInput facility_input(std::size_t units_per_loop,
                                             std::uint64_t revision) {
  cca_test::FacilitySpec spec;
  spec.zones = 2;
  spec.loops_per_zone = 2;
  spec.units_per_loop = units_per_loop;
  AccountingInput input = cca_test::make_facility(spec);
  input.generations.revision = StateRevision::of(revision).value();
  return input;
}

}  // namespace

// ---------------------------------------------------------------------------
// Publication in a child process, recovery in the parent
// ---------------------------------------------------------------------------

CCA_TEST(restart_recovers_generations_published_by_another_process) {
  TempDir dir("restart-publish");
  const std::filesystem::path root = dir.path() / "store";
  const std::filesystem::path report = dir.child("child-report.txt");

  // Three generations, published by a real independent process.
  const int child = run_child_process({"--child", "publish", root.string(), "3", "4",
                                       report.string()});
  CCA_CHECK_EQ(child, 0);
  const std::string published_report = child_report(report);
  CCA_CHECK_EQ(report_field(published_report, "status"), std::string("ok"));
  CCA_CHECK_EQ(report_field(published_report, "published"), std::string("3"));
  CCA_CHECK_EQ(report_field(published_report, "generation"), std::string("3"));
  CCA_CHECK(std::filesystem::is_directory(root));

  // This process recovers the authoritative generation.
  EngineOptions options;
  options.root = root;
  CCA_ASSIGN(engine, AccountingEngine::open(options));
  CCA_CHECK(engine.recovered_pending_revalidation());
  CCA_ASSIGN(status, engine.status());
  CCA_CHECK(status.open);
  CCA_CHECK(!status.shutting_down);
  CCA_CHECK(status.has_generation);
  CCA_CHECK(status.recovered_pending_revalidation);
  CCA_CHECK_EQ(status.generation.value(), std::uint64_t{3});
  CCA_CHECK_EQ(status.commit_sequence.value(), std::uint64_t{3});
  CCA_CHECK_EQ(status.retained_generations, static_cast<std::size_t>(3));

  CCA_ASSIGN(current, engine.current());
  CCA_CHECK(current.header().recovered);
  CCA_CHECK_EQ(current.header().generation.value(), std::uint64_t{3});
  CCA_CHECK_EQ(current.content_digest().to_hex(), status.generation_digest.to_hex());
  CCA_REQUIRE_OK(current.verify());
  CCA_CHECK(current.ledger().verify_closure().exact);

  // The recovered generation is complete: the whole facility the child
  // published is accounted, not a summary of it.
  CCA_CHECK_EQ(current.ledger().contributions().size(), static_cast<std::size_t>(4));
  CCA_CHECK_EQ(current.ledger().scopes().size(), static_cast<std::size_t>(7));
  // site_totals() covers the site cell alone; the whole facility is the
  // overall total, and it must carry every scope and contribution the child
  // published.
  const AccountingTotals totals = current.ledger().overall_totals();
  CCA_CHECK_EQ(totals.contribution_count, static_cast<std::size_t>(4));
  CCA_CHECK_EQ(totals.scope_count, static_cast<std::size_t>(7));
  CCA_CHECK(totals.declared_installed.milliwatts() > 0);
  const AccountingTotals site = current.ledger().site_totals();
  CCA_CHECK_EQ(site.scope_count, static_cast<std::size_t>(1));

  // Every retained generation is loadable and verifies in this process.
  CCA_ASSIGN(history, engine.history());
  CCA_CHECK_EQ(history.size(), static_cast<std::size_t>(3));
  for (const StoredGeneration& record : history) {
    CCA_ASSIGN(loaded, engine.load(record.generation));
    CCA_CHECK_EQ(loaded.content_digest().to_hex(), record.content_digest.to_hex());
    CCA_REQUIRE_OK(loaded.verify());
  }
  CCA_REQUIRE_OK(engine.close());

  // An independent process reads the same store back whole.
  const std::filesystem::path verify_report = dir.child("verify-report.txt");
  const int verifier =
      run_child_process({"--child", "verify", root.string(), verify_report.string()});
  CCA_CHECK_EQ(verifier, 0);
  const std::string verified = child_report(verify_report);
  CCA_CHECK_EQ(report_field(verified, "status"), std::string("ok"));
  CCA_CHECK_EQ(report_field(verified, "generation"), std::string("3"));
  CCA_CHECK_EQ(report_field(verified, "closure"), std::string("exact"));
  CCA_CHECK_EQ(report_field(verified, "recovered"), std::string("false"));
  CCA_CHECK_EQ(report_field(verified, "digest"), current.content_digest().to_hex());
}

CCA_TEST(restart_revalidate_clears_the_recovered_mark) {
  TempDir dir("restart-revalidate");
  const std::filesystem::path root = dir.path() / "store";
  const std::filesystem::path report = dir.child("publish-report.txt");
  const int child = run_child_process({"--child", "publish", root.string(), "2", "4",
                                       report.string()});
  CCA_CHECK_EQ(child, 0);
  CCA_CHECK_EQ(report_field(child_report(report), "generation"), std::string("2"));

  EngineOptions options;
  options.root = root;
  CCA_ASSIGN(engine, AccountingEngine::open(options));
  CCA_CHECK(engine.recovered_pending_revalidation());
  CCA_ASSIGN(recovered_status, engine.status());
  CCA_CHECK_EQ(recovered_status.generation.value(), std::uint64_t{2});
  CCA_CHECK_EQ(recovered_status.commit_sequence.value(), std::uint64_t{2});

  // A publication from recovered state is a revalidation, and it clears the
  // mark for every later reader.
  CCA_ASSIGN(attempt, AttemptId::from_material("restart-revalidate"));
  CCA_ASSIGN(revalidated, engine.revalidate(facility_input(5U, 2U), fixture_now(),
                                            fixture_now(), attempt));
  CCA_CHECK(!revalidated.header().recovered);
  CCA_CHECK_EQ(revalidated.header().generation.value(), std::uint64_t{3});
  CCA_CHECK_EQ(revalidated.header().commit_sequence.value(), std::uint64_t{3});
  CCA_REQUIRE_OK(revalidated.verify());
  CCA_CHECK(!engine.recovered_pending_revalidation());

  CCA_ASSIGN(fresh_status, engine.status());
  CCA_CHECK(!fresh_status.recovered_pending_revalidation);
  CCA_CHECK_EQ(fresh_status.generation.value(), std::uint64_t{3});
  CCA_CHECK_EQ(fresh_status.retained_generations, static_cast<std::size_t>(3));
  CCA_REQUIRE_OK(engine.close());

  // A later restart recovers the revalidated generation, which is a fresh
  // generation again: recovery is a property of this run, not of the bytes.
  CCA_ASSIGN(second_engine, AccountingEngine::open(options));
  CCA_CHECK(second_engine.recovered_pending_revalidation());
  CCA_ASSIGN(second_current, second_engine.current());
  CCA_CHECK(second_current.header().recovered);
  CCA_CHECK_EQ(second_current.header().generation.value(), std::uint64_t{3});
  CCA_REQUIRE_OK(second_current.verify());
  CCA_REQUIRE_OK(second_engine.close());
}

CCA_TEST(restart_many_open_close_cycles_leave_the_store_consistent) {
  TempDir dir("restart-cycles");
  const std::filesystem::path root = dir.path() / "store";
  const std::filesystem::path report = dir.child("cycles-report.txt");
  const int child = run_child_process({"--child", "publish", root.string(), "2", "3",
                                       report.string()});
  CCA_CHECK_EQ(child, 0);
  CCA_CHECK_EQ(report_field(child_report(report), "generation"), std::string("2"));

  EngineOptions options;
  options.root = root;
  constexpr int kCycles = 8;
  for (int cycle = 0; cycle < kCycles; ++cycle) {
    CCA_ASSIGN(engine, AccountingEngine::open(options));
    CCA_CHECK(engine.recovered_pending_revalidation());
    CCA_ASSIGN(status, engine.status());
    CCA_CHECK(status.has_generation);
    CCA_CHECK_EQ(status.generation.value(), std::uint64_t{2});
    CCA_CHECK_EQ(status.commit_sequence.value(), std::uint64_t{2});
    CCA_CHECK_EQ(status.retained_generations, static_cast<std::size_t>(2));
    CCA_ASSIGN(current, engine.current());
    CCA_CHECK_EQ(current.header().generation.value(), std::uint64_t{2});
    CCA_REQUIRE_OK(current.verify());
    CCA_ASSIGN(history, engine.history());
    CCA_CHECK_EQ(history.size(), static_cast<std::size_t>(2));
    CCA_CHECK_EQ(history[0].generation.value(), std::uint64_t{1});
    CCA_CHECK_EQ(history[1].generation.value(), std::uint64_t{2});
    CCA_REQUIRE_OK(engine.close());
  }

  // After the cycles the store is unchanged, and the read-only child agrees.
  const std::filesystem::path verify_report = dir.child("cycles-verify.txt");
  const int verifier =
      run_child_process({"--child", "verify", root.string(), verify_report.string()});
  CCA_CHECK_EQ(verifier, 0);
  const std::string verified = child_report(verify_report);
  CCA_CHECK_EQ(report_field(verified, "status"), std::string("ok"));
  CCA_CHECK_EQ(report_field(verified, "generation"), std::string("2"));
  CCA_CHECK_EQ(report_field(verified, "closure"), std::string("exact"));
}

// ---------------------------------------------------------------------------
// The writer lock across processes
// ---------------------------------------------------------------------------

CCA_TEST(restart_a_second_process_cannot_take_the_writer_lock) {
  TempDir dir("restart-lock");
  const std::filesystem::path root = dir.path() / "store";
  const std::filesystem::path ready = dir.child("writer-ready.txt");

  EngineOptions options;
  options.root = root;
  CCA_ASSIGN(engine, AccountingEngine::open(options));
  CCA_ASSIGN(attempt, AttemptId::from_material("restart-lock"));
  CCA_ASSIGN(published, engine.publish(facility_input(4U, 0U), fixture_now(),
                                       fixture_now(), attempt));

  // While this process holds the writer lock, the child cannot take it. It
  // reports the refusal in the error file next to its ready file and never
  // claims the lock by writing the ready file itself.
  const int blocked =
      run_child_process({"--child", "hold-writer", root.string(), ready.string(), "0"});
  CCA_CHECK_EQ(blocked, 2);
  CCA_CHECK(!std::filesystem::exists(ready));
  const std::string refusal = read_text_file(ready.string() + ".error");
  CCA_CHECK(refusal.find("StoreInUse") != std::string::npos);

  // A second child mode reaches the same conclusion.
  const std::filesystem::path reopen_report = dir.child("reopen-report.txt");
  const int reopen =
      run_child_process({"--child", "reopen-check", root.string(), reopen_report.string()});
  CCA_CHECK_EQ(reopen, 0);
  const std::string reopen_text = child_report(reopen_report);
  CCA_CHECK_EQ(report_field(reopen_text, "status"), std::string("error"));
  CCA_CHECK_EQ(report_field(reopen_text, "code"), std::string("StoreInUse"));

  // The refusals changed nothing: this process still holds the lock and the
  // generation it published.
  CCA_ASSIGN(status, engine.status());
  CCA_CHECK_EQ(status.generation.value(), published.header().generation.value());
  CCA_CHECK_EQ(status.commit_sequence.value(), std::uint64_t{1});
  CCA_REQUIRE_OK(engine.close());

  // Once the lock is released, a child process takes it and publishes.
  std::error_code code;
  static_cast<void>(std::filesystem::remove(ready, code));
  static_cast<void>(std::filesystem::remove(ready.string() + ".error", code));
  CCA_CHECK(!std::filesystem::exists(ready));
  const int holder =
      run_child_process({"--child", "hold-writer", root.string(), ready.string(), "0"});
  CCA_CHECK_EQ(holder, 0);
  CCA_CHECK(std::filesystem::exists(ready));
  CCA_CHECK_EQ(read_text_file(ready), std::string("ready"));

  const int reopened =
      run_child_process({"--child", "reopen-check", root.string(), reopen_report.string()});
  CCA_CHECK_EQ(reopened, 0);
  const std::string reopened_text = child_report(reopen_report);
  CCA_CHECK_EQ(report_field(reopened_text, "status"), std::string("ok"));
  CCA_CHECK_EQ(report_field(reopened_text, "generation"), std::string("1"));
  CCA_CHECK_EQ(report_field(reopened_text, "recovery"), std::string("pending"));
}
