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

#ifndef COOLING_CAPACITY_ACCOUNTING_SNAPSHOT_HPP
#define COOLING_CAPACITY_ACCOUNTING_SNAPSHOT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cooling_capacity_accounting/accounting.hpp"
#include "cooling_capacity_accounting/canonical.hpp"
#include "cooling_capacity_accounting/ids.hpp"
#include "cooling_capacity_accounting/ledger.hpp"
#include "cooling_capacity_accounting/limits.hpp"

namespace cooling_capacity_accounting {

/// Everything that identifies a published accounting generation, apart from its
/// payload.
struct SnapshotHeader {
  AccountGeneration generation;
  GenerationBundle generations;
  PolicyId policy;
  PolicyGeneration policy_generation;
  Digest policy_digest;
  Digest content_digest;
  Timestamp accounted_at;
  Timestamp published_at;
  IncarnationId writer;
  CommitSequence commit_sequence;
  /// Summary of the accounting, so a reader can decide whether to decode.
  AccountingTotals totals;
  ClosureStatus site_status = ClosureStatus::Empty;
  std::size_t finding_count = 0;
  std::size_t error_finding_count = 0;
  /// True when the state was recovered from the store and has not been
  /// revalidated against current evidence. Recovered state is not fresh
  /// physical evidence, and every answer derived from it says so.
  bool recovered = false;
  /// Set when the generation was produced by an explicit rollback to an older
  /// complete generation after integrity failure.
  std::uint64_t recovered_from_generation = 0;
};

/// An immutable, generation-bound accounting result together with the canonical
/// bytes that define it.
class AccountingSnapshot {
 public:
  AccountingSnapshot() = default;

  /// Accounts the input and encodes it. Fails when the input is invalid.
  [[nodiscard]] static Result<AccountingSnapshot> create(
      AccountingInput input, Timestamp accounted_at, const Limits& limits = Limits{});

  /// Rebuilds a snapshot from canonical bytes and a header. The header's
  /// content digest must match the bytes, or the snapshot is refused.
  [[nodiscard]] static Result<AccountingSnapshot> from_canonical(
      const std::vector<std::uint8_t>& canonical_bytes, SnapshotHeader header,
      const Limits& limits = Limits{});

  [[nodiscard]] const SnapshotHeader& header() const noexcept { return header_; }
  [[nodiscard]] const AccountingInput& input() const noexcept { return ledger_.input(); }
  [[nodiscard]] const AccountingLedger& ledger() const noexcept { return ledger_; }
  [[nodiscard]] const std::vector<std::uint8_t>& canonical_bytes() const noexcept {
    return canonical_bytes_;
  }
  [[nodiscard]] Digest content_digest() const noexcept {
    return header_.content_digest;
  }

  /// Re-decodes the canonical bytes, re-accounts them and compares both the
  /// content digest and the ledger digest. A snapshot that does not verify is
  /// not evidence of anything.
  [[nodiscard]] Result<void> verify(const Limits& limits = Limits{}) const;

  /// Stable, ASCII-only multi-line summary.
  [[nodiscard]] std::string summarize() const;

  SnapshotHeader& mutable_header() noexcept { return header_; }

 private:
  SnapshotHeader header_;
  AccountingLedger ledger_;
  std::vector<std::uint8_t> canonical_bytes_;
};

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_SNAPSHOT_HPP
