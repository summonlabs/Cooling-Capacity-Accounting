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

#include "cooling_capacity_accounting/accounting.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace cooling_capacity_accounting {
namespace {

struct EnumName {
  std::int32_t value;
  std::string_view name;
};

template <std::size_t N, typename Enum>
[[nodiscard]] std::string_view lookup(const std::array<EnumName, N>& table,
                                      Enum value) noexcept {
  for (const EnumName& entry : table) {
    if (entry.value == static_cast<std::int32_t>(value)) {
      return entry.name;
    }
  }
  return "Unknown";
}

constexpr std::array<EnumName, 3> kFindingSeverityNames{
    {{0, "Info"}, {1, "Warning"}, {2, "Error"}}};

constexpr std::array<EnumName, 30> kFindingCodeNames{{
    {1, "UnknownInstalledQuantity"},
    {2, "UnknownServiceState"},
    {3, "StaleEvidence"},
    {4, "SupersededEvidence"},
    {5, "FutureEvidence"},
    {6, "MissingEvidence"},
    {7, "MissingOutOfServiceEvidence"},
    {8, "MissingDerateEvidence"},
    {9, "MissingClassification"},
    {10, "NameplateMismatch"},
    {11, "UnexplainedResidual"},
    {12, "UnapportionedShare"},
    {13, "OverApportioned"},
    {14, "IndependenceNotDeclared"},
    {15, "MissingIndependenceEvidence"},
    {16, "ReserveShortfall"},
    {17, "ReserveUnresolved"},
    {18, "CoverageBelowMinimum"},
    {19, "MutualExclusionResolved"},
    {20, "SubstitutiveSpareWithheld"},
    {21, "AliasRecorded"},
    {22, "NonContributingClass"},
    {23, "MediumMismatch"},
    {24, "EmptyScope"},
    {25, "DeclaredTotalMissing"},
    {26, "DuplicateContributionIdentity"},
    {27, "ConflictingContributionIdentity"},
    {28, "UnsupportedQuantity"},
    {29, "AbsoluteDerateExceedsInstalled"},
    {30, "ReserveOnlyWithheld"},
}};

constexpr std::array<EnumName, 5> kWithheldReasonNames{{
    {0, "ReserveObligation"},
    {1, "ReserveOnlyClass"},
    {2, "SubstitutiveSpare"},
    {3, "MutualExclusion"},
    {4, "NonContributingClass"},
}};

constexpr std::array<EnumName, 5> kClosureStatusNames{{
    {0, "Empty"},
    {1, "Closed"},
    {2, "ClosedWithResidual"},
    {3, "Indeterminate"},
    {4, "Conflicting"},
}};

constexpr std::array<EnumName, 6> kObligationStatusNames{{
    {0, "Established"},
    {1, "NoReserveRequired"},
    {2, "IndependenceNotDeclared"},
    {3, "MissingIndependenceEvidence"},
    {4, "ProtectedQuantityUnknown"},
    {5, "Unresolved"},
}};

constexpr std::array<EnumName, 3> kRollupDimensionNames{
    {{0, "Scope"}, {1, "EquipmentClass"}, {2, "Medium"}}};

}  // namespace

std::string_view finding_severity_name(FindingSeverity severity) noexcept {
  return lookup(kFindingSeverityNames, severity);
}

std::string_view finding_code_name(FindingCode code) noexcept {
  return lookup(kFindingCodeNames, code);
}

std::string_view withheld_reason_name(WithheldReason reason) noexcept {
  return lookup(kWithheldReasonNames, reason);
}

void WithheldBreakdown::add(WithheldReason reason, ThermalPower amount) noexcept {
  const auto index = static_cast<std::size_t>(reason);
  if (index >= kWithheldReasonCount) {
    return;
  }
  by_reason[index] = ThermalPower::of_milliwatts(by_reason[index].milliwatts() +
                                                 amount.milliwatts());
  total = ThermalPower::of_milliwatts(total.milliwatts() + amount.milliwatts());
}

ThermalPower WithheldBreakdown::of(WithheldReason reason) const noexcept {
  const auto index = static_cast<std::size_t>(reason);
  if (index >= kWithheldReasonCount) {
    return ThermalPower::zero();
  }
  return by_reason[index];
}

Result<void> WithheldBreakdown::verify_total() const {
  std::int64_t sum = 0;
  for (const ThermalPower amount : by_reason) {
    sum += amount.milliwatts();
  }
  if (sum != total.milliwatts()) {
    return Error::of(ErrorCode::ClosureViolation,
                     "the withheld breakdown does not sum to its total")
        .with_detail("sum " + to_decimal(sum) + " total " +
                     to_decimal(total.milliwatts()));
  }
  return Ok{};
}

ThermalPower DispositionTotals::sum() const {
  return ThermalPower::of_milliwatts(allocatable.milliwatts() + withheld.milliwatts() +
                                     degraded_loss.milliwatts() +
                                     unavailable.milliwatts() +
                                     indeterminate.milliwatts());
}

Result<void> DispositionTotals::verify(const ThermalPower& declared_installed) const {
  const ThermalPower decomposed = sum();
  if (decomposed != declared_installed) {
    return Error::of(ErrorCode::ClosureViolation,
                     "the accounting identity does not hold")
        .with_detail("declared " + to_decimal(declared_installed.milliwatts()) +
                     " decomposed " + to_decimal(decomposed.milliwatts()));
  }
  return Ok{};
}

std::string_view closure_status_name(ClosureStatus status) noexcept {
  return lookup(kClosureStatusNames, status);
}

std::string_view obligation_status_name(ObligationStatus status) noexcept {
  return lookup(kObligationStatusNames, status);
}

std::string_view rollup_dimension_name(RollupDimension dimension) noexcept {
  return lookup(kRollupDimensionNames, dimension);
}

void AccountingTotals::add(const ScopeAccounting& scope) noexcept {
  scope_count += 1;
  contribution_count += scope.contribution_count;
  indeterminate_contribution_count += scope.indeterminate_contribution_count;
  unknown_installed_count += scope.unknown_installed_count;
  declared_installed = ThermalPower::of_milliwatts(declared_installed.milliwatts() +
                                                   scope.declared_installed.milliwatts());
  installed_fully_known = installed_fully_known && scope.installed_fully_known;
  totals.allocatable = ThermalPower::of_milliwatts(
      totals.allocatable.milliwatts() + scope.totals.allocatable.milliwatts());
  totals.withheld = ThermalPower::of_milliwatts(totals.withheld.milliwatts() +
                                                scope.totals.withheld.milliwatts());
  totals.degraded_loss = ThermalPower::of_milliwatts(
      totals.degraded_loss.milliwatts() + scope.totals.degraded_loss.milliwatts());
  totals.unavailable = ThermalPower::of_milliwatts(
      totals.unavailable.milliwatts() + scope.totals.unavailable.milliwatts());
  totals.indeterminate = ThermalPower::of_milliwatts(
      totals.indeterminate.milliwatts() + scope.totals.indeterminate.milliwatts());
  for (std::size_t index = 0; index < kWithheldReasonCount; ++index) {
    const auto reason = static_cast<WithheldReason>(index);
    withheld.add(reason, scope.withheld.of(reason));
  }
  residual = ThermalDelta::of_milliwatts(residual.milliwatts() +
                                         scope.residual.milliwatts());
  finding_count += scope.findings.size();
  for (const Finding& finding : scope.findings) {
    if (finding.severity == FindingSeverity::Error) {
      error_finding_count += 1;
    }
  }
}

}  // namespace cooling_capacity_accounting
