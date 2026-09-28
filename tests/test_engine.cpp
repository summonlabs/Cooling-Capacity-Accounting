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

// The engine lifecycle: open, status, publish, load, retention, cooperative
// shutdown and the single-writer fence.

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fixtures.hpp"
#include "test_harness.hpp"

using namespace cooling_capacity_accounting;
using cca_test::TempDir;
using cca_test::fixture_now;

namespace {

[[nodiscard]] AccountingInput facility_input(std::size_t units_per_loop) {
  cca_test::FacilitySpec spec;
  spec.zones = 2;
  spec.loops_per_zone = 2;
  spec.units_per_loop = units_per_loop;
  AccountingInput input = cca_test::make_facility(spec);
  input.generations.revision = StateRevision::of(units_per_loop).value();
  return input;
}

[[nodiscard]] Result<AttemptId> attempt_of(std::string_view label) {
  return AttemptId::from_material(label);
}

/// Publishes one generation under a caller-chosen attempt label.
[[nodiscard]] Result<AccountingSnapshot> publish_labeled(AccountingEngine& engine,
                                                         std::size_t units_per_loop,
                                                         std::string_view label,
                                                         std::int64_t published_offset) {
  CCA_TRY_ASSIGN(attempt, attempt_of(label));
  const Timestamp published = Timestamp::of_unix_milliseconds(
      fixture_now().unix_milliseconds() + published_offset);
  return engine.publish(facility_input(units_per_loop), fixture_now(), published, attempt);
}

[[nodiscard]] std::string generations_of(const std::vector<StoredGeneration>& history) {
  std::string text;
  for (const StoredGeneration& record : history) {
    if (!text.empty()) {
      text += ",";
    }
    text += to_decimal(record.generation.value());
  }
  return text;
}

}  // namespace

// ---------------------------------------------------------------------------
// Opening
// ---------------------------------------------------------------------------

CCA_TEST(engine_open_on_a_fresh_directory_reports_its_status) {
  TempDir dir("engine-open");
  EngineOptions options;
  options.root = dir.path() / "store";
  const std::string root_text = options.root.string();

  CCA_ASSIGN(engine, AccountingEngine::open(options));
  CCA_CHECK(!engine.shutting_down());
  CCA_CHECK(!engine.recovered_pending_revalidation());

  CCA_ASSIGN(status, engine.status());
  CCA_CHECK(status.open);
  CCA_CHECK(!status.read_only);
  CCA_CHECK(!status.shutting_down);
  CCA_CHECK(!status.recovered_pending_revalidation);
  CCA_CHECK(!status.has_generation);
  CCA_CHECK_EQ(status.generation.value(), AccountGeneration::initial().value());
  CCA_CHECK_EQ(status.commit_sequence.value(), CommitSequence::initial().value());
  CCA_CHECK_EQ(status.retained_generations, static_cast<std::size_t>(0));
  CCA_CHECK_EQ(status.store_root, root_text);
  CCA_CHECK(status.generation_digest.is_zero());
  CCA_CHECK_EQ(status.fence.epoch.value(), options.epoch.value());

  CCA_CHECK_CODE(engine.current(), ErrorCode::NoPublishedGeneration);
  CCA_ASSIGN(empty_history, engine.history());
  CCA_CHECK(empty_history.empty());
  CCA_CHECK_CODE(engine.load(AccountGeneration::initial()), ErrorCode::GenerationNotRetained);
  CCA_REQUIRE_OK(engine.close());
}

// ---------------------------------------------------------------------------
// Publication and the current generation
// ---------------------------------------------------------------------------

CCA_TEST(engine_publish_advances_the_generation_and_current_answers) {
  TempDir dir("engine-publish");
  EngineOptions options;
  options.root = dir.path() / "store";
  CCA_ASSIGN(engine, AccountingEngine::open(options));

  CCA_ASSIGN(first, publish_labeled(engine, 3, "engine-first", 0));
  // The first publication takes the initial generation; committed generations
  // are 1..N with no gap.
  CCA_CHECK_EQ(first.header().generation.value(), std::uint64_t{1});
  CCA_CHECK_EQ(first.header().commit_sequence.value(), std::uint64_t{1});
  CCA_CHECK(!first.header().recovered);
  CCA_CHECK_EQ(first.header().recovered_from_generation, std::uint64_t{0});
  CCA_REQUIRE_OK(first.verify());

  CCA_ASSIGN(current, engine.current());
  CCA_CHECK_EQ(current.header().generation.value(), first.header().generation.value());
  CCA_CHECK_EQ(current.content_digest().to_hex(), first.content_digest().to_hex());
  CCA_REQUIRE_OK(current.verify());

  CCA_ASSIGN(status, engine.status());
  CCA_CHECK(status.has_generation);
  CCA_CHECK_EQ(status.generation.value(), first.header().generation.value());
  CCA_CHECK_EQ(status.commit_sequence.value(), std::uint64_t{1});
  CCA_CHECK_EQ(status.retained_generations, static_cast<std::size_t>(1));
  CCA_CHECK_EQ(status.generation_digest.to_hex(), first.content_digest().to_hex());
  CCA_CHECK(!status.recovered_pending_revalidation);

  CCA_ASSIGN(second, publish_labeled(engine, 4, "engine-second", 1));
  CCA_CHECK_EQ(second.header().generation.value(), std::uint64_t{2});
  CCA_CHECK_EQ(second.header().commit_sequence.value(), std::uint64_t{2});
  CCA_ASSIGN(history, engine.history());
  CCA_CHECK_EQ(generations_of(history), std::string("1,2"));
  CCA_CHECK_EQ(history[0].commit_sequence.value(), std::uint64_t{1});
  CCA_CHECK_EQ(history[1].commit_sequence.value(), std::uint64_t{2});

  // Each retained generation is loadable and verifies.
  for (const StoredGeneration& record : history) {
    CCA_ASSIGN(loaded, engine.load(record.generation));
    CCA_CHECK_EQ(loaded.content_digest().to_hex(), record.content_digest.to_hex());
    CCA_REQUIRE_OK(loaded.verify());
  }
  CCA_REQUIRE_OK(engine.close());
}

CCA_TEST(engine_recovery_marks_the_authoritative_generation) {
  TempDir dir("engine-recovery");
  EngineOptions options;
  options.root = dir.path() / "store";

  CCA_ASSIGN(first_engine, AccountingEngine::open(options));
  CCA_ASSIGN(published, publish_labeled(first_engine, 3, "engine-recover", 0));
  CCA_CHECK(!first_engine.recovered_pending_revalidation());
  CCA_REQUIRE_OK(first_engine.close());

  // Reopening recovers the authoritative generation and marks it as pending
  // revalidation: it is state, not fresh physical evidence.
  CCA_ASSIGN(engine, AccountingEngine::open(options));
  CCA_CHECK(engine.recovered_pending_revalidation());
  CCA_ASSIGN(status, engine.status());
  CCA_CHECK(status.recovered_pending_revalidation);
  CCA_CHECK(status.has_generation);
  CCA_CHECK_EQ(status.generation.value(), published.header().generation.value());
  CCA_CHECK_EQ(status.generation_digest.to_hex(), published.content_digest().to_hex());
  CCA_CHECK_EQ(status.retained_generations, static_cast<std::size_t>(1));

  CCA_ASSIGN(current, engine.current());
  CCA_CHECK(current.header().recovered);
  CCA_CHECK_EQ(current.header().generation.value(), published.header().generation.value());
  CCA_CHECK_EQ(current.content_digest().to_hex(), published.content_digest().to_hex());
  CCA_REQUIRE_OK(current.verify());
  CCA_CHECK(current.ledger().verify_closure().exact);

  // revalidate() publishes fresh state and clears the mark.
  CCA_ASSIGN(fresh, publish_labeled(engine, 5, "engine-revalidate", 1));
  CCA_CHECK(!fresh.header().recovered);
  CCA_CHECK(!engine.recovered_pending_revalidation());
  CCA_CHECK_EQ(fresh.header().generation.value(), published.header().generation.value() + 1U);
  CCA_ASSIGN(status_after, engine.status());
  CCA_CHECK(!status_after.recovered_pending_revalidation);
  CCA_CHECK_EQ(status_after.generation.value(), fresh.header().generation.value());
  CCA_REQUIRE_OK(engine.close());
}

// ---------------------------------------------------------------------------
// Load, retention and history
// ---------------------------------------------------------------------------

CCA_TEST(engine_load_refuses_a_pruned_generation) {
  TempDir dir("engine-retention");
  EngineOptions options;
  options.root = dir.path() / "store";
  options.retention_generations = 2;
  CCA_ASSIGN(engine, AccountingEngine::open(options));

  for (std::size_t index = 0; index < 4U; ++index) {
    CCA_REQUIRE_OK(publish_labeled(engine, 3U + index,
                                   "engine-retention-" + std::to_string(index),
                                   static_cast<std::int64_t>(index)));
  }
  CCA_ASSIGN(history, engine.history());
  CCA_CHECK_EQ(generations_of(history), std::string("3,4"));
  CCA_ASSIGN(status, engine.status());
  CCA_CHECK_EQ(status.retained_generations, static_cast<std::size_t>(2));
  CCA_CHECK_EQ(status.generation.value(), std::uint64_t{4});

  CCA_CHECK_CODE(engine.load(AccountGeneration::of(1U).value()),
                 ErrorCode::GenerationNotRetained);
  CCA_CHECK_CODE(engine.load(AccountGeneration::of(2U).value()),
                 ErrorCode::GenerationNotRetained);
  CCA_ASSIGN(oldest, engine.load(AccountGeneration::of(3U).value()));
  CCA_REQUIRE_OK(oldest.verify());
  CCA_ASSIGN(latest, engine.load(AccountGeneration::of(4U).value()));
  CCA_REQUIRE_OK(latest.verify());
  CCA_CHECK_EQ(latest.content_digest().to_hex(),
               engine.status().value().generation_digest.to_hex());
  CCA_REQUIRE_OK(engine.close());
}

// ---------------------------------------------------------------------------
// Shutdown and close
// ---------------------------------------------------------------------------

CCA_TEST(engine_begin_shutdown_is_idempotent_and_stops_publication) {
  TempDir dir("engine-shutdown");
  EngineOptions options;
  options.root = dir.path() / "store";
  CCA_ASSIGN(engine, AccountingEngine::open(options));
  CCA_ASSIGN(published, publish_labeled(engine, 3, "engine-shutdown", 0));

  CCA_REQUIRE_OK(engine.begin_shutdown());
  CCA_CHECK(engine.shutting_down());
  CCA_REQUIRE_OK(engine.begin_shutdown());
  CCA_CHECK(engine.shutting_down());

  CCA_ASSIGN(status, engine.status());
  CCA_CHECK(status.shutting_down);
  CCA_CHECK(status.open);

  // Every later publication is refused with the lifecycle code.
  CCA_ASSIGN(attempt, attempt_of("engine-shutdown-after"));
  CCA_CHECK_CODE(engine.publish(facility_input(3), fixture_now(), fixture_now(), attempt),
                 ErrorCode::ShuttingDown);
  CCA_CHECK_CODE(engine.revalidate(facility_input(3), fixture_now(), fixture_now(), attempt),
                 ErrorCode::UnexpectedState);

  // Queries keep working and still answer from the last committed generation.
  CCA_ASSIGN(current, engine.current());
  CCA_CHECK_EQ(current.header().generation.value(), published.header().generation.value());
  CCA_CHECK_EQ(current.content_digest().to_hex(), published.content_digest().to_hex());
  CCA_ASSIGN(history, engine.history());
  CCA_CHECK_EQ(generations_of(history), std::string("1"));

  CCA_REQUIRE_OK(engine.close());
  CCA_CHECK(engine.shutting_down());
}

CCA_TEST(engine_close_is_safe_twice_and_publish_after_close_is_unexpected_state) {
  TempDir dir("engine-close");
  EngineOptions options;
  options.root = dir.path() / "store";
  CCA_ASSIGN(engine, AccountingEngine::open(options));
  CCA_REQUIRE_OK(publish_labeled(engine, 3, "engine-close", 0));

  CCA_REQUIRE_OK(engine.close());
  CCA_REQUIRE_OK(engine.close());

  CCA_ASSIGN(attempt, attempt_of("engine-after-close"));
  CCA_CHECK_CODE(engine.publish(facility_input(3), fixture_now(), fixture_now(), attempt),
                 ErrorCode::UnexpectedState);
  CCA_CHECK_CODE(engine.revalidate(facility_input(3), fixture_now(), fixture_now(), attempt),
                 ErrorCode::UnexpectedState);
  CCA_CHECK_CODE(engine.current(), ErrorCode::UnexpectedState);
  CCA_CHECK_CODE(engine.history(), ErrorCode::UnexpectedState);
  CCA_CHECK_CODE(engine.load(AccountGeneration::of(2U).value()),
                 ErrorCode::UnexpectedState);
  CCA_ASSIGN(status, engine.status());
  CCA_CHECK(!status.open);

  // The writer lock was released, so the root can be opened again.
  CCA_ASSIGN(reopened, AccountingEngine::open(options));
  CCA_CHECK_EQ(reopened.status().value().generation.value(), std::uint64_t{1});
  CCA_CHECK(reopened.recovered_pending_revalidation());
  CCA_REQUIRE_OK(reopened.close());
}

CCA_TEST(engine_revalidate_requires_a_recovered_generation) {
  TempDir dir("engine-revalidate");
  EngineOptions options;
  options.root = dir.path() / "store";
  CCA_ASSIGN(engine, AccountingEngine::open(options));

  CCA_ASSIGN(attempt, attempt_of("engine-revalidate-fresh"));
  CCA_CHECK_CODE(engine.revalidate(facility_input(3), fixture_now(), fixture_now(), attempt),
                 ErrorCode::UnexpectedState);

  // Publishing fresh state does not create anything to revalidate.
  CCA_REQUIRE_OK(publish_labeled(engine, 3, "engine-revalidate-publish", 0));
  CCA_CHECK(!engine.recovered_pending_revalidation());
  CCA_CHECK_CODE(engine.revalidate(facility_input(3), fixture_now(), fixture_now(), attempt),
                 ErrorCode::UnexpectedState);
  CCA_REQUIRE_OK(engine.close());
}

// ---------------------------------------------------------------------------
// The writer fence
// ---------------------------------------------------------------------------

CCA_TEST(engine_two_engines_on_one_root_cannot_both_write) {
  TempDir dir("engine-lock");
  EngineOptions options;
  options.root = dir.path() / "store";
  CCA_ASSIGN(first, AccountingEngine::open(options));

  CCA_CHECK_CODE(AccountingEngine::open(options), ErrorCode::StoreInUse);

  StoreOptions store_options;
  store_options.root = options.root;
  CCA_CHECK_CODE(AccountingStore::open(store_options), ErrorCode::StoreInUse);

  // The refused attempts changed nothing.
  CCA_REQUIRE_OK(publish_labeled(first, 3, "engine-lock-first", 0));
  CCA_ASSIGN(status, first.status());
  CCA_CHECK_EQ(status.generation.value(), std::uint64_t{1});
  CCA_CHECK_EQ(status.commit_sequence.value(), std::uint64_t{1});
  CCA_REQUIRE_OK(first.close());

  CCA_ASSIGN(second, AccountingEngine::open(options));
  CCA_CHECK_EQ(second.status().value().generation.value(), std::uint64_t{1});
  CCA_REQUIRE_OK(second.close());
}
