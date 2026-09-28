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

#ifndef COOLING_CAPACITY_ACCOUNTING_ENGINE_HPP
#define COOLING_CAPACITY_ACCOUNTING_ENGINE_HPP

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "cooling_capacity_accounting/errors.hpp"
#include "cooling_capacity_accounting/ids.hpp"
#include "cooling_capacity_accounting/limits.hpp"
#include "cooling_capacity_accounting/snapshot.hpp"
#include "cooling_capacity_accounting/store.hpp"

namespace cooling_capacity_accounting {

/// How an engine opens its store.
struct EngineOptions {
  std::filesystem::path root;
  Limits limits;
  bool create_if_missing = true;
  IncarnationId incarnation;
  ControlPlaneEpoch epoch = ControlPlaneEpoch::initial();
  std::size_t retention_generations = kMaxRetainedGenerations;
};

/// A snapshot of the engine's own state.
struct EngineStatus {
  bool open = false;
  bool read_only = false;
  bool shutting_down = false;
  /// True while the loaded generation came from the store and has not been
  /// revalidated against current evidence.
  bool recovered_pending_revalidation = false;
  bool has_generation = false;
  AccountGeneration generation;
  CommitSequence commit_sequence;
  WriterFence fence;
  std::size_t retained_generations = 0;
  std::string store_root;
  Digest generation_digest;
};

/// Composes a durable store, an accounting policy and the accounting engine.
///
/// The engine owns the lifecycle: it opens the store (taking the cross-process
/// writer lock), recovers the authoritative generation, publishes new
/// generations under a fenced request, and answers queries from the immutable
/// snapshot it holds. It starts no thread. Shutdown is cooperative and
/// idempotent: once shutdown has begun no new generation is published, and a
/// query still returns the last committed generation.
class AccountingEngine {
 public:
  AccountingEngine();
  ~AccountingEngine();

  AccountingEngine(const AccountingEngine&) = delete;
  AccountingEngine& operator=(const AccountingEngine&) = delete;
  AccountingEngine(AccountingEngine&& other) noexcept;
  AccountingEngine& operator=(AccountingEngine&& other) noexcept;

  [[nodiscard]] static Result<AccountingEngine> open(const EngineOptions& options);

  /// Accounts and publishes one generation. Fails when the engine is shutting
  /// down, when the store is read-only, or when any fence does not match.
  [[nodiscard]] Result<AccountingSnapshot> publish(AccountingInput input,
                                                   Timestamp accounted_at,
                                                   Timestamp published_at,
                                                   AttemptId attempt);

  /// Publishes a generation that replaces recovered state with freshly
  /// examined state. Until this succeeds, answers derived from a recovered
  /// generation are labelled as pending revalidation.
  [[nodiscard]] Result<AccountingSnapshot> revalidate(AccountingInput fresh,
                                                      Timestamp accounted_at,
                                                      Timestamp published_at,
                                                      AttemptId attempt);

  /// The generation the engine currently holds.
  [[nodiscard]] Result<AccountingSnapshot> current() const;
  [[nodiscard]] Result<AccountingSnapshot> load(AccountGeneration generation) const;
  [[nodiscard]] Result<std::vector<StoredGeneration>> history() const;

  /// Stops accepting new work. Idempotent; queries keep working.
  [[nodiscard]] Result<void> begin_shutdown();
  [[nodiscard]] bool shutting_down() const noexcept;
  [[nodiscard]] bool recovered_pending_revalidation() const noexcept;
  [[nodiscard]] Result<EngineStatus> status() const;

  /// Shuts down and releases the writer lock. Safe to call twice.
  [[nodiscard]] Result<void> close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_ENGINE_HPP
