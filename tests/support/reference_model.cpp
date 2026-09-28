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

#include "reference_model.hpp"

#include <map>
#include <string>

namespace cca_test {

using namespace cooling_capacity_accounting;
namespace {

/// Exact composition of the declared derates, written the way the documented
/// semantics describe it rather than the way the ledger implements it: absolute
/// reductions are subtracted first, then the factor product is divided once.
/// The product is exact in the 128-bit domain; the primitive itself is pinned
/// independently by test_wide against hand-computed products and limits.
[[nodiscard]] Result<ThermalPower> reference_derate(
    ThermalPower installed, const std::vector<DerateFactor>& derates) {
  std::int64_t remaining = installed.milliwatts();
  WideUInt128 numerator = wide_multiply(static_cast<std::uint64_t>(remaining), 1U);
  std::uint64_t denominator = 1;
  for (const DerateFactor& derate : derates) {
    if (derate.kind == DerateKind::Absolute) {
      remaining -= derate.absolute.milliwatts();
      if (remaining < 0) {
        remaining = 0;
      }
      numerator = wide_multiply(static_cast<std::uint64_t>(remaining), 1U);
      continue;
    }
    CCA_TRY_ASSIGN(scaled,
                   wide_mul_u64(numerator, static_cast<std::uint64_t>(derate.factor.ppm())));
    numerator = scaled;
    denominator *= static_cast<std::uint64_t>(Ratio::scale());
  }
  CCA_TRY_ASSIGN(retained, wide_div_u64(numerator, denominator));
  return ThermalPower::of_milliwatts(static_cast<std::int64_t>(retained));
}

}  // namespace

Result<ReferenceModel> ReferenceModel::build(const AccountingInput& input) {
  ReferenceModel model;
  for (const AccountingScope& scope : input.scopes) {
    model.scopes_[scope.id.str()] = ReferenceTotals{};
  }
  for (const Contribution& contribution : input.contributions) {
    if (contribution.classification != ContributionClass::Additive) {
      return Error::of(ErrorCode::NotSupported,
                       "the reference model covers additive contributions only");
    }
    if (contribution.sharing.kind != Sharing::Kind::Exclusive) {
      return Error::of(ErrorCode::NotSupported,
                       "the reference model covers exclusive sharing only");
    }
    if (!contribution.installed.is_known()) {
      return Error::of(ErrorCode::NotSupported,
                       "the reference model covers known quantities only");
    }
    const std::string home = contribution.home_scope.str();
    const auto entry = model.scopes_.find(home);
    if (entry == model.scopes_.end()) {
      return Error::of(ErrorCode::UnknownScope, "the reference model needs every scope");
    }
    const std::int64_t installed = contribution.installed.value().milliwatts();
    entry->second.declared_installed += installed;
    switch (contribution.service) {
      case ServiceState::OutOfService:
        entry->second.unavailable += installed;
        break;
      case ServiceState::InService:
      case ServiceState::Degraded: {
        CCA_TRY_ASSIGN(retained,
                       reference_derate(ThermalPower::of_milliwatts(installed),
                                        contribution.derates));
        entry->second.degraded_loss += installed - retained.milliwatts();
        entry->second.allocatable += retained.milliwatts();
        break;
      }
      case ServiceState::Unknown:
      case ServiceState::Unspecified:
        entry->second.indeterminate += installed;
        break;
    }
  }
  return model;
}

const ReferenceTotals& ReferenceModel::scope(const std::string& name) const {
  static const ReferenceTotals kEmpty;
  const auto entry = scopes_.find(name);
  return entry == scopes_.end() ? kEmpty : entry->second;
}

ReferenceTotals ReferenceModel::overall() const {
  ReferenceTotals total;
  for (const auto& entry : scopes_) {
    total.declared_installed += entry.second.declared_installed;
    total.allocatable += entry.second.allocatable;
    total.withheld += entry.second.withheld;
    total.degraded_loss += entry.second.degraded_loss;
    total.unavailable += entry.second.unavailable;
    total.indeterminate += entry.second.indeterminate;
  }
  return total;
}

}  // namespace cca_test
