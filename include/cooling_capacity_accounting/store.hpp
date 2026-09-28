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

#ifndef COOLING_CAPACITY_ACCOUNTING_STORE_HPP
#define COOLING_CAPACITY_ACCOUNTING_STORE_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "cooling_capacity_accounting/errors.hpp"
#include "cooling_capacity_accounting/ids.hpp"
#include "cooling_capacity_accounting/limits.hpp"
#include "cooling_capacity_accounting/snapshot.hpp"

namespace cooling_capacity_accounting {

/// How a store is opened.
struct StoreOptions {
  std::filesystem::path root;
  Limits limits;
  /// Create the directory when it does not exist.
  bool create_if_missing = true;
  /// Identity of the writing incarnation. A zero identifier means "derive one
  /// for this process".
  IncarnationId incarnation;
  /// The control-plane epoch the writer asserts. Publishing under an epoch
  /// older than the one recorded in the store is refused.
  ControlPlaneEpoch epoch = ControlPlaneEpoch::initial();
  /// How many published generations stay readable.
  std::size_t retention_generations = kMaxRetainedGenerations;
};

/// One committed generation as the store describes it.
struct StoredGeneration {
  AccountGeneration generation;
  CommitSequence commit_sequence;
  ControlPlaneEpoch epoch;
  GenerationBundle generations;
  PolicyId policy;
  PolicyGeneration policy_generation;
  Digest policy_digest;
  Digest content_digest;
  Timestamp accounted_at;
  Timestamp published_at;
  std::uint64_t payload_bytes = 0;
  bool recovered = false;
  std::uint64_t recovered_from_generation = 0;
};

/// A request to publish one accounting generation.
///
/// The request is fenced on every axis it depends on: the generation the caller
/// believed was current, the state revision it was planned against, and the
/// control-plane epoch it holds. A request that does not match the store is
/// refused rather than applied.
struct PublishRequest {
  /// Identity of this attempt. An identical retry replays the original result;
  /// a different request reusing the identity is a conflict.
  AttemptId attempt;
  /// Digest of the exact intended content. It is what makes a retry
  /// recognisable as the same request.
  Digest fingerprint;
  /// The generation the caller believes is current.
  AccountGeneration expected_previous;
  /// The revision the caller planned against.
  StateRevision expected_revision;
  ControlPlaneEpoch epoch;
  Timestamp published_at;
};

struct PublishOutcome {
  StoredGeneration record;
  /// True when the outcome was replayed from a recorded attempt instead of
  /// actuating the publish again.
  bool replayed = false;
};

/// The outcome of an explicit rollback to an older complete generation.
struct RecoveryReport {
  AccountGeneration adopted;
  AccountGeneration abandoned;
  Digest adopted_digest;
  std::string reason;
};

/// A durable, integrity-checked store of published accounting generations with
/// single-writer cross-process authority.
///
/// Layout inside the store root:
///
///   LOCK          writer fence file, held with an operating-system lock
///   MANIFEST      the commit point; names the authoritative generation
///   gen-<n>.cca   one committed generation payload each
///   staging-*     in-flight writes that are never authoritative
///
/// The manifest rename is the commit point. A crash before it leaves no
/// committed generation; a crash after it leaves exactly the new one. Staging
/// files are removed when the store is opened.
class AccountingStore {
 public:
  AccountingStore();
  ~AccountingStore();

  AccountingStore(const AccountingStore&) = delete;
  AccountingStore& operator=(const AccountingStore&) = delete;
  AccountingStore(AccountingStore&& other) noexcept;
  AccountingStore& operator=(AccountingStore&& other) noexcept;

  /// Opens the store for writing and takes the cross-process writer lock.
  [[nodiscard]] static Result<AccountingStore> open(const StoreOptions& options);
  /// Opens the store for reading under a shared lock. Publishing is refused.
  [[nodiscard]] static Result<AccountingStore> open_read_only(const StoreOptions& options);

  /// Validates the root path without opening it: rejects traversal components,
  /// embedded NULs, over-long paths, reparse points and ambiguous spellings.
  [[nodiscard]] static Result<void> validate_root(const std::filesystem::path& root);

  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] bool read_only() const noexcept;
  [[nodiscard]] const StoreOptions& options() const noexcept;
  [[nodiscard]] WriterFence fence() const noexcept;
  /// The authoritative generation. An empty store reports the initial
  /// generation and has_generation() == false.
  [[nodiscard]] AccountGeneration current_generation() const noexcept;
  [[nodiscard]] bool has_generation() const noexcept;
  [[nodiscard]] CommitSequence commit_sequence() const noexcept;

  /// Publishes one accounting generation.
  [[nodiscard]] Result<PublishOutcome> publish(const AccountingSnapshot& snapshot,
                                               const PublishRequest& request);

  [[nodiscard]] Result<AccountingSnapshot> latest() const;
  [[nodiscard]] Result<AccountingSnapshot> load(AccountGeneration generation) const;
  /// Every retained generation, oldest first. Each one is read back from disk
  /// and verified before it is reported, so a corrupt retained generation is an
  /// error here rather than a wrong answer later.
  [[nodiscard]] Result<std::vector<StoredGeneration>> history() const;

  /// Explicitly abandons an unusable latest generation and adopts the highest
  /// older complete one. Never happens silently: the store records the rollback
  /// in the new manifest and in the adopted generation's header.
  [[nodiscard]] Result<RecoveryReport> adopt_previous_generation();

  /// Releases the writer lock and closes every handle. Safe to call twice.
  [[nodiscard]] Result<void> close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_STORE_HPP
