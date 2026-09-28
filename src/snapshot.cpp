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

#include "cooling_capacity_accounting/snapshot.hpp"

#include <string>
#include <utility>

namespace cooling_capacity_accounting {
namespace {

/// The closure status of the whole generation, using the same precedence a
/// single scope uses: Conflicting > Indeterminate > ClosedWithResidual >
/// Closed > Empty. A site cell is often empty while its zones and loops are not,
/// so the summary must be taken over the whole generation rather than over the
/// site cells alone.
[[nodiscard]] ClosureStatus site_status_of(const AccountingLedger& ledger) {
  bool any_content = false;
  bool residual = false;
  for (const ScopeAccounting& record : ledger.scopes()) {
    if (record.status == ClosureStatus::Conflicting) {
      return ClosureStatus::Conflicting;
    }
    if (record.status == ClosureStatus::Indeterminate) {
      any_content = true;
      residual = residual || !record.residual.is_zero();
      continue;
    }
    if (record.status == ClosureStatus::ClosedWithResidual) {
      any_content = true;
      residual = true;
      continue;
    }
    if (record.status == ClosureStatus::Closed) {
      any_content = true;
    }
  }
  bool indeterminate = false;
  for (const ScopeAccounting& record : ledger.scopes()) {
    if (record.status == ClosureStatus::Indeterminate) {
      indeterminate = true;
    }
  }
  if (indeterminate) {
    return ClosureStatus::Indeterminate;
  }
  if (residual) {
    return ClosureStatus::ClosedWithResidual;
  }
  return any_content ? ClosureStatus::Closed : ClosureStatus::Empty;
}

void fill_derived_header(SnapshotHeader& header, const AccountingLedger& ledger) {
  header.generations = ledger.generations();
  header.policy = ledger.policy().id;
  header.policy_generation = ledger.policy().generation;
  header.policy_digest = ledger.policy().digest();
  header.accounted_at = ledger.accounted_at();
  header.totals = ledger.overall_totals();
  header.site_status = site_status_of(ledger);
  header.finding_count = ledger.findings().size();
  header.error_finding_count = 0;
  for (const Finding& finding : ledger.findings()) {
    if (finding.severity == FindingSeverity::Error) {
      header.error_finding_count += 1;
    }
  }
}

}  // namespace

Result<AccountingSnapshot> AccountingSnapshot::create(AccountingInput input,
                                                      Timestamp accounted_at,
                                                      const Limits& limits) {
  CCA_TRY_ASSIGN(ledger, AccountingLedger::build(std::move(input), accounted_at, limits));
  CCA_TRY_ASSIGN(bytes, encode_canonical(ledger.input(), limits));

  AccountingSnapshot snapshot;
  snapshot.ledger_ = std::move(ledger);
  snapshot.canonical_bytes_ = bytes;
  snapshot.header_.content_digest =
      Digest::of_bytes(snapshot.canonical_bytes_.data(), snapshot.canonical_bytes_.size());
  fill_derived_header(snapshot.header_, snapshot.ledger_);
  snapshot.header_.published_at = accounted_at;
  return snapshot;
}

Result<AccountingSnapshot> AccountingSnapshot::from_canonical(
    const std::vector<std::uint8_t>& canonical_bytes, SnapshotHeader header,
    const Limits& limits) {
  const Digest computed =
      Digest::of_bytes(canonical_bytes.data(), canonical_bytes.size());
  if (computed != header.content_digest) {
    return Error::of(ErrorCode::DigestMismatch,
                     "the canonical payload does not match the recorded digest")
        .with_detail("recorded " + header.content_digest.to_hex() + " computed " +
                     computed.to_hex());
  }
  CCA_TRY_ASSIGN(input, decode_canonical(canonical_bytes, limits));
  CCA_TRY_ASSIGN(ledger, AccountingLedger::build(std::move(input), header.accounted_at,
                                                 limits));
  AccountingSnapshot snapshot;
  snapshot.ledger_ = std::move(ledger);
  snapshot.canonical_bytes_ = canonical_bytes;
  snapshot.header_ = std::move(header);
  fill_derived_header(snapshot.header_, snapshot.ledger_);
  snapshot.header_.content_digest = computed;
  return snapshot;
}

Result<void> AccountingSnapshot::verify(const Limits& limits) const {
  const Digest computed =
      Digest::of_bytes(canonical_bytes_.data(), canonical_bytes_.size());
  if (computed != header_.content_digest) {
    return Error::of(ErrorCode::DigestMismatch,
                     "the stored canonical payload does not match its digest");
  }
  CCA_TRY_ASSIGN(input, decode_canonical(canonical_bytes_, limits));
  CCA_TRY_ASSIGN(rebuilt, AccountingLedger::build(std::move(input), header_.accounted_at,
                                                  limits));
  if (rebuilt.digest() != ledger_.digest()) {
    return Error::of(ErrorCode::IntegrityFailure,
                     "re-accounting the canonical payload produced a different digest")
        .with_detail("recorded " + ledger_.digest().to_hex() + " rebuilt " +
                     rebuilt.digest().to_hex());
  }
  const ClosureCheck closure = rebuilt.verify_closure();
  if (!closure.exact) {
    return Error::of(ErrorCode::ClosureViolation,
                     "the accounting identity does not hold for every scope")
        .with_detail(to_decimal(static_cast<std::uint64_t>(closure.violations.size())) +
                     " violations");
  }
  return Ok{};
}

std::string AccountingSnapshot::summarize() const {
  std::string text;
  text += "generation=";
  text += to_decimal(header_.generation.value());
  text += " commit_sequence=";
  text += to_decimal(header_.commit_sequence.value());
  text += " recovery=";
  text += header_.recovered ? "recovered" : "fresh";
  text += " site_status=";
  text += std::string(closure_status_name(header_.site_status));
  text += " scopes=";
  text += to_decimal(static_cast<std::uint64_t>(header_.totals.scope_count));
  text += " contributions=";
  text += to_decimal(static_cast<std::uint64_t>(header_.totals.contribution_count));
  text += " declared_mw=";
  text += to_decimal(header_.totals.declared_installed.milliwatts());
  text += " allocatable_mw=";
  text += to_decimal(header_.totals.totals.allocatable.milliwatts());
  text += " withheld_mw=";
  text += to_decimal(header_.totals.totals.withheld.milliwatts());
  text += " degraded_loss_mw=";
  text += to_decimal(header_.totals.totals.degraded_loss.milliwatts());
  text += " unavailable_mw=";
  text += to_decimal(header_.totals.totals.unavailable.milliwatts());
  text += " indeterminate_mw=";
  text += to_decimal(header_.totals.totals.indeterminate.milliwatts());
  text += " residual_mw=";
  text += to_decimal(header_.totals.residual.milliwatts());
  text += " findings=";
  text += to_decimal(static_cast<std::uint64_t>(header_.finding_count));
  text += " errors=";
  text += to_decimal(static_cast<std::uint64_t>(header_.error_finding_count));
  text += " content_digest=";
  text += header_.content_digest.to_hex();
  return text;
}

}  // namespace cooling_capacity_accounting
