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

// The durable store: publication, fencing, idempotent retry, retention,
// history and the read-only and single-writer rules. Everything this file
// asserts about the store is observed through its public API.

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fixtures.hpp"
#include "test_harness.hpp"

using namespace cooling_capacity_accounting;
using cca_test::TempDir;
using cca_test::fixture_now;
using cca_test::id_of;

namespace {

/// A valid snapshot of the synthetic facility, with a caller-chosen revision.
[[nodiscard]] Result<AccountingSnapshot> facility_snapshot(std::size_t units_per_loop,
                                                           std::uint64_t revision) {
  cca_test::FacilitySpec spec;
  spec.zones = 2;
  spec.loops_per_zone = 2;
  spec.units_per_loop = units_per_loop;
  AccountingInput input = cca_test::make_facility(spec);
  CCA_TRY_ASSIGN(revision_value, StateRevision::of(revision));
  input.generations.revision = revision_value;
  return AccountingSnapshot::create(std::move(input), fixture_now());
}

struct PublishPlan {
  std::string attempt;
  std::string fingerprint;
  AccountGeneration expected_previous;
  Timestamp published_at = fixture_now();
  ControlPlaneEpoch epoch = ControlPlaneEpoch::initial();
};

[[nodiscard]] Result<PublishOutcome> publish_with(AccountingStore& store,
                                                  const AccountingSnapshot& snapshot,
                                                  const PublishPlan& plan) {
  CCA_TRY_ASSIGN(attempt, AttemptId::from_material(plan.attempt));
  PublishRequest request;
  request.attempt = attempt;
  request.fingerprint = Digest::of_text(plan.fingerprint);
  request.expected_previous = plan.expected_previous;
  request.expected_revision = snapshot.header().generations.revision;
  request.epoch = plan.epoch;
  request.published_at = plan.published_at;
  return store.publish(snapshot, request);
}

/// Publishes one generation with a distinct attempt and fingerprint.
[[nodiscard]] Result<PublishOutcome> publish_next(AccountingStore& store,
                                                  std::size_t units_per_loop,
                                                  std::uint64_t revision,
                                                  std::string_view label) {
  CCA_TRY_ASSIGN(snapshot, facility_snapshot(units_per_loop, revision));
  PublishPlan plan;
  plan.attempt = std::string(label);
  plan.fingerprint = std::string(label) + "-fingerprint";
  plan.expected_previous = store.current_generation();
  plan.published_at = Timestamp::of_unix_milliseconds(
      fixture_now().unix_milliseconds() + static_cast<std::int64_t>(revision));
  return publish_with(store, snapshot, plan);
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

[[nodiscard]] std::string generations_of_result(
    const Result<std::vector<StoredGeneration>>& history) {
  return history.ok() ? generations_of(history.value()) : std::string("<error>");
}

}  // namespace

// ---------------------------------------------------------------------------
// An empty store
// ---------------------------------------------------------------------------

CCA_TEST(persistence_empty_store_reports_the_initial_generation) {
  TempDir dir("persistence-empty");
  StoreOptions options;
  options.root = dir.path() / "store";

  CCA_ASSIGN(store, AccountingStore::open(options));
  CCA_CHECK(store.is_open());
  CCA_CHECK(!store.read_only());
  CCA_CHECK(!store.has_generation());
  CCA_CHECK_EQ(store.current_generation().value(), AccountGeneration::initial().value());
  CCA_CHECK_EQ(store.commit_sequence().value(), CommitSequence::initial().value());
  CCA_CHECK_EQ(store.fence().last_commit.value(), CommitSequence::initial().value());
  CCA_CHECK_EQ(store.fence().epoch.value(), options.epoch.value());
  CCA_CHECK_CODE(store.latest(), ErrorCode::NoPublishedGeneration);
  CCA_ASSIGN(empty_history, store.history());
  CCA_CHECK(empty_history.empty());
  CCA_REQUIRE_OK(store.close());

  // The empty store survives a reopen and is still empty.
  CCA_ASSIGN(reopened, AccountingStore::open(options));
  CCA_CHECK(!reopened.has_generation());
  CCA_CHECK_EQ(reopened.current_generation().value(), AccountGeneration::initial().value());
  CCA_CHECK_EQ(reopened.commit_sequence().value(), CommitSequence::initial().value());
  CCA_REQUIRE_OK(reopened.close());
}

// ---------------------------------------------------------------------------
// Publication
// ---------------------------------------------------------------------------

CCA_TEST(persistence_first_publish_commits_one_generation) {
  TempDir dir("persistence-first");
  StoreOptions options;
  options.root = dir.path() / "store";
  CCA_ASSIGN(store, AccountingStore::open(options));

  // The first publication takes the initial generation itself: committed
  // generations are 1..N with no reserved number and no gap. An empty store
  // reports the same value with has_generation() == false, which is what
  // distinguishes "nothing committed yet" from "generation 1 is committed".
  const AccountGeneration first = AccountGeneration::initial();
  const CommitSequence first_commit = CommitSequence::initial().next().value();
  CCA_CHECK_EQ(first.value(), std::uint64_t{1});
  CCA_CHECK_EQ(first_commit.value(), std::uint64_t{1});

  CCA_ASSIGN(snapshot, facility_snapshot(3, 0));
  PublishPlan plan;
  plan.attempt = "attempt-one";
  plan.fingerprint = "fingerprint-one";
  plan.expected_previous = store.current_generation();
  CCA_ASSIGN(outcome, publish_with(store, snapshot, plan));
  CCA_CHECK(!outcome.replayed);
  CCA_CHECK_EQ(outcome.record.generation.value(), first.value());
  CCA_CHECK_EQ(outcome.record.commit_sequence.value(), first_commit.value());
  CCA_CHECK_EQ(outcome.record.content_digest.to_hex(), snapshot.content_digest().to_hex());
  CCA_CHECK_EQ(outcome.record.epoch.value(), options.epoch.value());
  CCA_CHECK_EQ(outcome.record.payload_bytes,
               static_cast<std::uint64_t>(snapshot.canonical_bytes().size()));

  CCA_CHECK(store.has_generation());
  CCA_CHECK_EQ(store.current_generation().value(), first.value());
  CCA_CHECK_EQ(store.commit_sequence().value(), first_commit.value());

  CCA_ASSIGN(history, store.history());
  CCA_CHECK_EQ(history.size(), static_cast<std::size_t>(1));
  CCA_CHECK_EQ(history[0].generation.value(), first.value());
  CCA_CHECK_EQ(history[0].commit_sequence.value(), first_commit.value());
  CCA_CHECK_EQ(history[0].content_digest.to_hex(), snapshot.content_digest().to_hex());
  CCA_CHECK(!history[0].recovered);
  CCA_CHECK_EQ(history[0].recovered_from_generation, std::uint64_t{0});

  CCA_ASSIGN(latest, store.latest());
  CCA_CHECK_EQ(latest.header().generation.value(), first.value());
  CCA_CHECK_EQ(latest.header().commit_sequence.value(), first_commit.value());
  CCA_REQUIRE_OK(latest.verify());
  CCA_CHECK(latest.ledger().verify_closure().exact);
  CCA_ASSIGN(by_generation, store.load(first));
  CCA_CHECK_EQ(by_generation.content_digest().to_hex(), snapshot.content_digest().to_hex());
  CCA_REQUIRE_OK(by_generation.verify());
  CCA_REQUIRE_OK(store.close());
}

CCA_TEST(persistence_several_publishes_advance_generation_and_commit_sequence) {
  TempDir dir("persistence-advance");
  StoreOptions options;
  options.root = dir.path() / "store";
  CCA_ASSIGN(store, AccountingStore::open(options));

  constexpr std::size_t kPublishes = 4;
  for (std::size_t index = 0; index < kPublishes; ++index) {
    CCA_ASSIGN(outcome, publish_next(store, 2U + index, index,
                                     "advance-" + std::to_string(index)));
    CCA_CHECK(!outcome.replayed);
    CCA_CHECK_EQ(outcome.record.generation.value(), index + 1U);
    CCA_CHECK_EQ(outcome.record.commit_sequence.value(), index + 1U);
    CCA_CHECK_EQ(store.current_generation().value(), index + 1U);
    CCA_CHECK_EQ(store.commit_sequence().value(), index + 1U);
  }

  CCA_ASSIGN(history, store.history());
  CCA_CHECK_EQ(history.size(), kPublishes);
  for (std::size_t index = 0; index < history.size(); ++index) {
    CCA_CHECK_EQ(history[index].generation.value(), index + 1U);
    CCA_CHECK_EQ(history[index].commit_sequence.value(), index + 1U);
    CCA_ASSIGN(loaded, store.load(history[index].generation));
    CCA_CHECK_EQ(loaded.content_digest().to_hex(), history[index].content_digest.to_hex());
    CCA_REQUIRE_OK(loaded.verify());
  }
  // History is oldest first.
  for (std::size_t index = 1; index < history.size(); ++index) {
    CCA_CHECK(history[index - 1U].generation < history[index].generation);
    CCA_CHECK(history[index - 1U].commit_sequence < history[index].commit_sequence);
  }
  CCA_REQUIRE_OK(store.close());

  // The published generations are still there after a reopen.
  CCA_ASSIGN(reopened, AccountingStore::open(options));
  CCA_CHECK_EQ(reopened.current_generation().value(), kPublishes);
  CCA_CHECK_EQ(reopened.commit_sequence().value(), kPublishes);
  CCA_ASSIGN(reopened_history, reopened.history());
  CCA_CHECK_EQ(reopened_history.size(), kPublishes);
  CCA_REQUIRE_OK(reopened.close());
}

// ---------------------------------------------------------------------------
// Fencing and idempotency
// ---------------------------------------------------------------------------

CCA_TEST(persistence_replays_an_identical_attempt_without_advancing) {
  TempDir dir("persistence-replay");
  StoreOptions options;
  options.root = dir.path() / "store";
  CCA_ASSIGN(store, AccountingStore::open(options));

  CCA_ASSIGN(snapshot, facility_snapshot(3, 0));
  PublishPlan plan;
  plan.attempt = "replay-attempt";
  plan.fingerprint = "replay-fingerprint";
  plan.expected_previous = store.current_generation();
  CCA_ASSIGN(first, publish_with(store, snapshot, plan));
  CCA_CHECK(!first.replayed);
  CCA_CHECK_EQ(first.record.generation.value(), std::uint64_t{1});
  CCA_CHECK_EQ(first.record.commit_sequence.value(), std::uint64_t{1});

  // The same attempt with the same fingerprint replays the recorded outcome.
  plan.expected_previous = store.current_generation();
  CCA_ASSIGN(second, publish_with(store, snapshot, plan));
  CCA_CHECK(second.replayed);
  CCA_CHECK_EQ(second.record.generation.value(), first.record.generation.value());
  CCA_CHECK_EQ(second.record.commit_sequence.value(), first.record.commit_sequence.value());
  CCA_CHECK_EQ(second.record.content_digest.to_hex(), first.record.content_digest.to_hex());
  CCA_CHECK_EQ(second.record.published_at.unix_milliseconds(),
               first.record.published_at.unix_milliseconds());

  // Nothing was actuated: the commit sequence and the generation are unchanged.
  CCA_CHECK_EQ(store.current_generation().value(), first.record.generation.value());
  CCA_CHECK_EQ(store.commit_sequence().value(), first.record.commit_sequence.value());
  CCA_ASSIGN(history, store.history());
  CCA_CHECK_EQ(history.size(), static_cast<std::size_t>(1));

  // A third identical retry replays as well.
  CCA_ASSIGN(third, publish_with(store, snapshot, plan));
  CCA_CHECK(third.replayed);
  CCA_CHECK_EQ(third.record.generation.value(), first.record.generation.value());
  CCA_CHECK_EQ(store.commit_sequence().value(), first.record.commit_sequence.value());

  // The same attempt with a different fingerprint is a conflict.
  PublishPlan conflicting = plan;
  conflicting.fingerprint = "a-different-fingerprint";
  CCA_CHECK_CODE(publish_with(store, snapshot, conflicting), ErrorCode::IdempotencyConflict);
  CCA_CHECK_EQ(store.commit_sequence().value(), first.record.commit_sequence.value());

  // A fresh attempt planned against a generation that is not current is refused
  // rather than applied.
  PublishPlan stale;
  stale.attempt = "stale-attempt";
  stale.fingerprint = "stale-fingerprint";
  stale.expected_previous =
      AccountGeneration::of(first.record.generation.value() + 4U).value();
  CCA_CHECK_CODE(publish_with(store, snapshot, stale),
                 ErrorCode::PublishPreconditionFailed);
  CCA_CHECK_EQ(store.current_generation().value(), first.record.generation.value());
  CCA_CHECK_EQ(store.commit_sequence().value(), first.record.commit_sequence.value());

  // A retry that still carries the generation it saw replays once more, and the
  // commit sequence still does not move.
  plan.expected_previous = store.current_generation();
  CCA_ASSIGN(replayed_again, publish_with(store, snapshot, plan));
  CCA_CHECK(replayed_again.replayed);
  CCA_CHECK_EQ(replayed_again.record.generation.value(), first.record.generation.value());
  CCA_CHECK_EQ(store.commit_sequence().value(), first.record.commit_sequence.value());
  CCA_REQUIRE_OK(store.close());
}

CCA_TEST(persistence_engine_retry_does_not_publish_twice) {
  TempDir dir("persistence-engine-retry");
  EngineOptions options;
  options.root = dir.path() / "store";
  CCA_ASSIGN(engine, AccountingEngine::open(options));

  cca_test::FacilitySpec spec;
  spec.zones = 2;
  spec.loops_per_zone = 2;
  spec.units_per_loop = 3;
  const AccountingInput input = cca_test::make_facility(spec);
  CCA_ASSIGN(attempt, AttemptId::from_material("engine-retry"));
  const Timestamp published = fixture_now();

  CCA_ASSIGN(first, engine.publish(input, fixture_now(), published, attempt));
  CCA_ASSIGN(second, engine.publish(input, fixture_now(), published, attempt));
  CCA_CHECK_EQ(second.header().generation.value(), first.header().generation.value());
  CCA_CHECK_EQ(second.content_digest().to_hex(), first.content_digest().to_hex());
  CCA_ASSIGN(status, engine.status());
  CCA_CHECK_EQ(status.generation.value(), first.header().generation.value());
  CCA_CHECK_EQ(status.commit_sequence.value(), first.header().commit_sequence.value());
  CCA_CHECK_EQ(status.retained_generations, static_cast<std::size_t>(1));

  // A different attempt publishes the next generation.
  CCA_ASSIGN(other_attempt, AttemptId::from_material("engine-retry-next"));
  CCA_ASSIGN(third, engine.publish(input, fixture_now(), published, other_attempt));
  CCA_CHECK_EQ(third.header().generation.value(), first.header().generation.value() + 1U);
  CCA_REQUIRE_OK(engine.close());
}

// ---------------------------------------------------------------------------
// Retention and history
// ---------------------------------------------------------------------------

CCA_TEST(persistence_retention_prunes_the_oldest_generation) {
  TempDir dir("persistence-retention");
  StoreOptions options;
  options.root = dir.path() / "store";
  options.retention_generations = 3;
  CCA_ASSIGN(store, AccountingStore::open(options));

  constexpr std::size_t kPublishes = 5;
  for (std::size_t index = 0; index < kPublishes; ++index) {
    CCA_ASSIGN(outcome, publish_next(store, 2U + index, index,
                                     "retention-" + std::to_string(index)));
    CCA_CHECK(!outcome.replayed);
  }
  // Generations 2 and 3 were pruned; 4, 5 and 6 stay readable.
  CCA_CHECK_EQ(store.current_generation().value(), kPublishes);
  CCA_ASSIGN(history, store.history());
  CCA_CHECK_EQ(generations_of(history), std::string("3,4,5"));

  CCA_CHECK_CODE(store.load(AccountGeneration::of(1U).value()),
                 ErrorCode::GenerationNotRetained);
  CCA_CHECK_CODE(store.load(AccountGeneration::of(2U).value()),
                 ErrorCode::GenerationNotRetained);
  CCA_ASSIGN(oldest_retained, store.load(AccountGeneration::of(3U).value()));
  CCA_REQUIRE_OK(oldest_retained.verify());
  CCA_ASSIGN(latest, store.latest());
  CCA_CHECK_EQ(latest.header().generation.value(), kPublishes);

  // Retention survives a reopen.
  CCA_REQUIRE_OK(store.close());
  CCA_ASSIGN(reopened, AccountingStore::open(options));
  CCA_ASSIGN(reopened_history, reopened.history());
  CCA_CHECK_EQ(generations_of(reopened_history), std::string("3,4,5"));
  CCA_CHECK_CODE(reopened.load(AccountGeneration::of(1U).value()),
                 ErrorCode::GenerationNotRetained);

  // The next publication prunes one more.
  CCA_ASSIGN(next_outcome, publish_next(reopened, 9, 5, "retention-next"));
  CCA_CHECK_EQ(next_outcome.record.generation.value(), std::uint64_t{6});
  CCA_ASSIGN(pruned_history, reopened.history());
  CCA_CHECK_EQ(generations_of(pruned_history), std::string("4,5,6"));
  CCA_CHECK_CODE(reopened.load(AccountGeneration::of(3U).value()),
                 ErrorCode::GenerationNotRetained);
  CCA_REQUIRE_OK(reopened.close());
}

CCA_TEST(persistence_history_is_oldest_first_and_every_record_verifies) {
  TempDir dir("persistence-history");
  StoreOptions options;
  options.root = dir.path() / "store";
  CCA_ASSIGN(store, AccountingStore::open(options));

  for (std::size_t index = 0; index < 4U; ++index) {
    CCA_REQUIRE_OK(publish_next(store, 3U, index, "history-" + std::to_string(index)));
  }
  CCA_ASSIGN(history, store.history());
  CCA_CHECK_EQ(generations_of(history), std::string("1,2,3,4"));
  for (std::size_t index = 0; index < history.size(); ++index) {
    const StoredGeneration& record = history[index];
    CCA_CHECK_EQ(record.generation.value(), index + 1U);
    CCA_CHECK_EQ(record.commit_sequence.value(), index + 1U);
    CCA_CHECK_EQ(record.epoch.value(), options.epoch.value());
    CCA_CHECK(!record.recovered);
    CCA_CHECK_EQ(record.recovered_from_generation, std::uint64_t{0});
    CCA_CHECK(record.payload_bytes > 0U);
    CCA_ASSIGN(loaded, store.load(record.generation));
    CCA_CHECK_EQ(loaded.header().generation.value(), record.generation.value());
    CCA_CHECK_EQ(loaded.header().commit_sequence.value(), record.commit_sequence.value());
    CCA_CHECK_EQ(loaded.content_digest().to_hex(), record.content_digest.to_hex());
    CCA_CHECK_EQ(loaded.header().policy.str(), record.policy.str());
    CCA_CHECK_EQ(loaded.header().policy_digest.to_hex(), record.policy_digest.to_hex());
    CCA_REQUIRE_OK(loaded.verify());
  }
  CCA_REQUIRE_OK(store.close());
}

// ---------------------------------------------------------------------------
// Read-only access
// ---------------------------------------------------------------------------

CCA_TEST(persistence_read_only_store_refuses_publishing_and_changes_nothing) {
  TempDir dir("persistence-readonly");
  StoreOptions options;
  options.root = dir.path() / "store";
  CCA_ASSIGN(writer, AccountingStore::open(options));
  CCA_REQUIRE_OK(publish_next(writer, 3, 0, "readonly-first"));
  CCA_REQUIRE_OK(publish_next(writer, 3, 1, "readonly-second"));
  const std::uint64_t generation_before = writer.current_generation().value();
  const std::uint64_t commit_before = writer.commit_sequence().value();
  CCA_REQUIRE_OK(writer.close());

  CCA_ASSIGN(reader, AccountingStore::open_read_only(options));
  CCA_CHECK(reader.is_open());
  CCA_CHECK(reader.read_only());
  CCA_CHECK(reader.has_generation());
  CCA_CHECK_EQ(reader.current_generation().value(), generation_before);
  CCA_CHECK_EQ(reader.commit_sequence().value(), commit_before);
  CCA_ASSIGN(history_before, reader.history());
  CCA_CHECK_EQ(generations_of(history_before), std::string("1,2"));
  CCA_ASSIGN(latest, reader.latest());
  CCA_REQUIRE_OK(latest.verify());

  CCA_ASSIGN(snapshot, facility_snapshot(4, 2));
  PublishPlan plan;
  plan.attempt = "read-only-attempt";
  plan.fingerprint = "read-only-fingerprint";
  plan.expected_previous = reader.current_generation();
  CCA_CHECK_CODE(publish_with(reader, snapshot, plan), ErrorCode::NotSupported);
  CCA_CHECK_CODE(reader.adopt_previous_generation(), ErrorCode::NotSupported);

  // Nothing changed: the generator, the commit sequence and every record are
  // exactly what they were.
  CCA_CHECK_EQ(reader.current_generation().value(), generation_before);
  CCA_CHECK_EQ(reader.commit_sequence().value(), commit_before);
  CCA_ASSIGN(history_after, reader.history());
  CCA_CHECK_EQ(generations_of(history_after), generations_of(history_before));
  for (std::size_t index = 0; index < history_after.size(); ++index) {
    CCA_CHECK_EQ(history_after[index].content_digest.to_hex(),
                 history_before[index].content_digest.to_hex());
    CCA_CHECK_EQ(history_after[index].commit_sequence.value(),
                 history_before[index].commit_sequence.value());
  }
  CCA_REQUIRE_OK(reader.close());

  // The writer sees exactly the same state afterwards.
  CCA_ASSIGN(reopened, AccountingStore::open(options));
  CCA_CHECK_EQ(reopened.current_generation().value(), generation_before);
  CCA_CHECK_EQ(reopened.commit_sequence().value(), commit_before);
  CCA_ASSIGN(reopened_history, reopened.history());
  CCA_CHECK_EQ(generations_of(reopened_history), std::string("1,2"));
  CCA_REQUIRE_OK(reopened.close());
}

// ---------------------------------------------------------------------------
// Single writer
// ---------------------------------------------------------------------------

CCA_TEST(persistence_second_store_on_the_same_root_is_refused) {
  TempDir dir("persistence-lock");
  StoreOptions options;
  options.root = dir.path() / "store";
  CCA_ASSIGN(first, AccountingStore::open(options));
  CCA_CHECK(first.is_open());

  CCA_CHECK_CODE(AccountingStore::open(options), ErrorCode::StoreInUse);

  // The refusal is not a poisoned state: the first store still works.
  CCA_REQUIRE_OK(publish_next(first, 3, 0, "lock-first"));
  CCA_CHECK_EQ(first.current_generation().value(), std::uint64_t{1});

  CCA_REQUIRE_OK(first.close());
  CCA_ASSIGN(second, AccountingStore::open(options));
  CCA_CHECK(second.is_open());
  CCA_CHECK_EQ(second.current_generation().value(), std::uint64_t{1});
  CCA_REQUIRE_OK(second.close());
}

CCA_TEST(persistence_close_is_idempotent_and_the_store_reopens) {
  TempDir dir("persistence-close");
  StoreOptions options;
  options.root = dir.path() / "store";

  CCA_ASSIGN(store, AccountingStore::open(options));
  CCA_REQUIRE_OK(store.close());
  CCA_REQUIRE_OK(store.close());
  CCA_CHECK(!store.is_open());
  CCA_CHECK_CODE(store.publish(AccountingSnapshot(), PublishRequest()),
                 ErrorCode::UnexpectedState);
  CCA_CHECK_CODE(store.latest(), ErrorCode::UnexpectedState);
  CCA_CHECK_CODE(store.history(), ErrorCode::UnexpectedState);
  CCA_CHECK_CODE(store.adopt_previous_generation(), ErrorCode::UnexpectedState);

  CCA_ASSIGN(reopened, AccountingStore::open(options));
  CCA_CHECK(reopened.is_open());
  CCA_REQUIRE_OK(publish_next(reopened, 3, 0, "close-first"));
  CCA_REQUIRE_OK(reopened.close());

  CCA_ASSIGN(third, AccountingStore::open(options));
  CCA_CHECK_EQ(third.current_generation().value(), std::uint64_t{1});
  CCA_REQUIRE_OK(third.close());
  CCA_REQUIRE_OK(third.close());
}

// ---------------------------------------------------------------------------
// Control-plane epoch
// ---------------------------------------------------------------------------

CCA_TEST(persistence_refuses_a_stale_or_cross_epoch_publish) {
  TempDir dir("persistence-epoch");
  StoreOptions options;
  options.root = dir.path() / "store";
  // A store that does not exist yet is created under the writer's epoch.
  options.epoch = ControlPlaneEpoch::of(2).value();
  CCA_ASSIGN(store, AccountingStore::open(options));
  CCA_CHECK_EQ(store.fence().epoch.value(), std::uint64_t{2});

  CCA_ASSIGN(snapshot, facility_snapshot(3, 0));
  PublishPlan plan;
  plan.attempt = "epoch-attempt";
  plan.fingerprint = "epoch-fingerprint";
  plan.expected_previous = store.current_generation();

  // An epoch older than the store's is stale; a different newer one is a
  // different authority.
  plan.epoch = ControlPlaneEpoch::of(1).value();
  CCA_CHECK_CODE(publish_with(store, snapshot, plan), ErrorCode::StaleEpoch);
  plan.epoch = ControlPlaneEpoch::of(3).value();
  CCA_CHECK_CODE(publish_with(store, snapshot, plan), ErrorCode::CrossEpochAuthority);
  CCA_CHECK(!store.has_generation());
  CCA_CHECK_EQ(store.commit_sequence().value(), CommitSequence::initial().value());

  plan.epoch = options.epoch;
  CCA_ASSIGN(outcome, publish_with(store, snapshot, plan));
  CCA_CHECK_EQ(outcome.record.generation.value(), std::uint64_t{1});
  CCA_CHECK_EQ(outcome.record.epoch.value(), std::uint64_t{2});
  CCA_REQUIRE_OK(store.close());

  // A writer holding a different epoch than the store was written under cannot
  // open it for writing.
  StoreOptions older = options;
  older.epoch = ControlPlaneEpoch::of(1).value();
  CCA_CHECK_CODE(AccountingStore::open(older), ErrorCode::StaleEpoch);
  StoreOptions newer = options;
  newer.epoch = ControlPlaneEpoch::of(3).value();
  CCA_CHECK_CODE(AccountingStore::open(newer), ErrorCode::CrossEpochAuthority);

  CCA_ASSIGN(reopened, AccountingStore::open(options));
  CCA_CHECK_EQ(reopened.current_generation().value(), std::uint64_t{1});
  CCA_REQUIRE_OK(reopened.close());
}
