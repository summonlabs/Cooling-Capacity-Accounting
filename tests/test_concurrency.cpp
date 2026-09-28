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

// Concurrency proof. The library starts no thread of its own and takes no lock
// outside the store's writer lock, so what has to be proven is that a published
// immutable snapshot is safe to read from many threads at once and that
// repeated open/close cycles are safe.

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

namespace {

using namespace cooling_capacity_accounting;

[[nodiscard]] AccountingInput concurrent_facility() {
  cca_test::FacilitySpec spec;
  spec.zones = 3;
  spec.loops_per_zone = 2;
  spec.units_per_loop = 8;
  return cca_test::make_facility(spec);
}

}  // namespace

CCA_TEST(concurrency_immutable_snapshot_is_safe_to_read_from_many_threads) {
  const AccountingInput input = concurrent_facility();
  CCA_ASSIGN(ledger, AccountingLedger::build(input, cca_test::fixture_now()));
  const Digest expected_digest = ledger.digest();
  const AccountingTotals expected_totals = ledger.overall_totals();
  const RollupView expected_rollup =
      ledger.scope_rollup(cca_test::id_of<ScopeId>("site.alpha")).value();

  constexpr int kThreads = 8;
  constexpr int kIterations = 50;
  std::atomic<int> mismatches{0};
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int index = 0; index < kThreads; ++index) {
    workers.emplace_back([&ledger, &mismatches, &expected_digest, &expected_totals,
                          &expected_rollup]() {
      for (int iteration = 0; iteration < kIterations; ++iteration) {
        if (ledger.digest() != expected_digest) {
          mismatches.fetch_add(1);
        }
        const AccountingTotals totals = ledger.overall_totals();
        if (totals.declared_installed != expected_totals.declared_installed ||
            totals.totals.allocatable != expected_totals.totals.allocatable) {
          mismatches.fetch_add(1);
        }
        const Result<RollupView> rollup =
            ledger.scope_rollup(cca_test::id_of<ScopeId>("site.alpha"));
        if (!rollup.ok() || rollup.value().digest != expected_rollup.digest) {
          mismatches.fetch_add(1);
        }
        const ClosureCheck closure = ledger.verify_closure();
        if (!closure.exact) {
          mismatches.fetch_add(1);
        }
        const std::string report = ledger.to_report();
        if (report.empty()) {
          mismatches.fetch_add(1);
        }
      }
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }
  CCA_CHECK_EQ(mismatches.load(), 0);
}

CCA_TEST(concurrency_independent_builds_do_not_interfere) {
  constexpr int kThreads = 6;
  std::atomic<int> failures{0};
  std::vector<Digest> digests(static_cast<std::size_t>(kThreads));
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int index = 0; index < kThreads; ++index) {
    workers.emplace_back([index, &failures, &digests]() {
      cca_test::FacilitySpec spec;
      spec.zones = 2;
      spec.loops_per_zone = 2;
      spec.units_per_loop = static_cast<std::size_t>(2 + index);
      const AccountingInput input = cca_test::make_facility(spec);
      const Result<AccountingLedger> ledger =
          AccountingLedger::build(input, cca_test::fixture_now());
      if (!ledger.ok()) {
        failures.fetch_add(1);
        return;
      }
      digests[static_cast<std::size_t>(index)] = ledger.value().digest();
      if (!ledger.value().verify_closure().exact) {
        failures.fetch_add(1);
      }
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }
  CCA_CHECK_EQ(failures.load(), 0);
  for (const Digest& digest : digests) {
    CCA_CHECK(!digest.is_zero());
  }
  // Different unit counts must produce different generations.
  for (std::size_t left = 0; left < digests.size(); ++left) {
    for (std::size_t right = left + 1; right < digests.size(); ++right) {
      CCA_CHECK(digests[left] != digests[right]);
    }
  }
}

CCA_TEST(concurrency_repeated_open_and_close_is_safe) {
  cca_test::TempDir workspace("concurrency-open-close");
  const AccountingInput input = concurrent_facility();
  CCA_ASSIGN(first_snapshot,
             AccountingSnapshot::create(input, cca_test::fixture_now()));

  StoreOptions options;
  options.root = workspace.path();
  options.create_if_missing = true;

  {
    CCA_ASSIGN(store, AccountingStore::open(options));
    PublishRequest request;
    request.attempt = cca_test::attempt_of("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
    request.fingerprint = first_snapshot.content_digest();
    request.expected_previous = store.current_generation();
    request.expected_revision = first_snapshot.header().generations.revision;
    request.epoch = store.fence().epoch;
    request.published_at = cca_test::fixture_now();
    CCA_ASSIGN(outcome, store.publish(first_snapshot, request));
    CCA_CHECK_EQ(outcome.record.generation.value(), 1U);
    CCA_CHECK(store.close().ok());
    CCA_CHECK(store.close().ok());  // closing twice is not an error
  }

  for (int cycle = 0; cycle < 40; ++cycle) {
    CCA_ASSIGN(store, AccountingStore::open(options));
    CCA_CHECK(store.has_generation());
    CCA_CHECK_EQ(store.current_generation().value(), 1U);
    CCA_ASSIGN(snapshot, store.latest());
    CCA_CHECK(snapshot.content_digest() == first_snapshot.content_digest());
    CCA_CHECK(store.close().ok());
  }

  // A read-only store shares the lock, so several readers may hold it at once
  // while no writer does.
  std::vector<AccountingStore> readers;
  for (int index = 0; index < 4; ++index) {
    CCA_ASSIGN(reader, AccountingStore::open_read_only(options));
    CCA_CHECK(reader.read_only());
    readers.push_back(std::move(reader));
  }
  CCA_CHECK_EQ(readers.size(), static_cast<std::size_t>(4));
  for (AccountingStore& reader : readers) {
    CCA_ASSIGN(snapshot, reader.latest());
    CCA_CHECK(snapshot.content_digest() == first_snapshot.content_digest());
  }
  for (AccountingStore& reader : readers) {
    CCA_CHECK(reader.close().ok());
  }
}
