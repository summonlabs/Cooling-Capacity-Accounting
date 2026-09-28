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

#ifndef COOLING_CAPACITY_ACCOUNTING_SRC_INTERNAL_HPP
#define COOLING_CAPACITY_ACCOUNTING_SRC_INTERNAL_HPP

// Declarations shared between the accounting implementation files. Nothing here
// is installed or exported: it is the private seam between validation,
// canonical encoding and the ledger.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "cooling_capacity_accounting/accounting.hpp"
#include "cooling_capacity_accounting/contribution.hpp"
#include "cooling_capacity_accounting/digest.hpp"
#include "cooling_capacity_accounting/errors.hpp"
#include "cooling_capacity_accounting/evidence.hpp"
#include "cooling_capacity_accounting/ledger.hpp"
#include "cooling_capacity_accounting/limits.hpp"
#include "cooling_capacity_accounting/policy.hpp"

namespace cooling_capacity_accounting {
namespace internal {

/// Digest of one contribution's content, used to tell a replay of the same
/// record from a conflicting record that reuses an identity.
[[nodiscard]] Digest contribution_content_digest(const Contribution& contribution);
/// Digest of one evidence record's content.
[[nodiscard]] Digest evidence_content_digest(const EvidenceRecord& record);
/// Digest of one group's content.
[[nodiscard]] Digest group_content_digest(const ContributionGroup& group);
/// Digest of one scope's content.
[[nodiscard]] Digest scope_content_digest(const AccountingScope& scope);

/// The input after validation and identity resolution.
struct ResolvedInput {
  AccountingInput input;
  /// Findings produced while resolving identities: deduplicated replays and
  /// preserved conflicts.
  std::vector<Finding> findings;
  /// Records that lost an identity conflict. They are preserved here rather
  /// than dropped, and they are never counted.
  std::vector<Contribution> conflicting_contributions;
};

/// Validates an input and resolves duplicate identities.
///
/// The first failure is deterministic: validation runs in the documented
/// precedence order (usage, input, structure, reference, quantity, policy) and
/// within a stage in canonical record order, so the same invalid input always
/// yields the same primary error.
[[nodiscard]] Result<ResolvedInput> validate_and_resolve(const AccountingInput& input,
                                                         Timestamp accounted_at,
                                                         const Limits& limits);

/// Applies the declared derates to an installed quantity and returns the
/// retained quantity. Absolute derates are subtracted first, in DerateId order;
/// the remaining factors are then composed exactly as
/// floor(retained * product(ppm) / 1000000^k) with a checked 128-bit
/// intermediate, so the result does not depend on the order the factors were
/// supplied in.
[[nodiscard]] Result<ThermalPower> apply_derates(
    ThermalPower installed, const std::vector<DerateFactor>& derates);

/// Sorted copy of a vector by a key extractor, so every iteration order in the
/// implementation is documented and total.
template <typename T, typename Key>
[[nodiscard]] std::vector<T> sorted_by(const std::vector<T>& values, Key key) {
  std::vector<T> copy = values;
  std::sort(copy.begin(), copy.end(), [&key](const T& lhs, const T& rhs) {
    return key(lhs) < key(rhs);
  });
  return copy;
}

/// Sorts a vector in place by a key extractor.
template <typename T, typename Key>
void sort_by(std::vector<T>& values, Key key) {
  std::sort(values.begin(), values.end(), [&key](const T& lhs, const T& rhs) {
    return key(lhs) < key(rhs);
  });
}

/// Renders a finding as one stable line.
[[nodiscard]] std::string render_finding(const Finding& finding);

/// True when the scope kind fixes a single medium rather than accepting both.
[[nodiscard]] bool scope_kind_fixes_medium(ScopeKind kind) noexcept;

/// Deterministic ordering key for a contribution inside its group.
struct MemberOrderKey {
  std::uint32_t priority;
  std::string id;
};

[[nodiscard]] MemberOrderKey member_order_key(const Contribution& contribution);
[[nodiscard]] bool member_order_less(const Contribution& lhs, const Contribution& rhs);

}  // namespace internal
}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_SRC_INTERNAL_HPP
