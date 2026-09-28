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

#ifndef COOLING_CAPACITY_ACCOUNTING_POLICY_HPP
#define COOLING_CAPACITY_ACCOUNTING_POLICY_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "cooling_capacity_accounting/domain.hpp"
#include "cooling_capacity_accounting/errors.hpp"
#include "cooling_capacity_accounting/limits.hpp"
#include "cooling_capacity_accounting/text.hpp"
#include "cooling_capacity_accounting/units.hpp"

namespace cooling_capacity_accounting {

/// The declaration rules an accounting generation is evaluated under.
///
/// The policy can tighten or relax *evidentiary* requirements and set
/// tolerances. It deliberately has no switch that turns unknown into zero,
/// stale into current, or an unevidenced independence claim into a redundancy
/// guarantee: those are properties of the accounting semantics, not of policy.
struct AccountingPolicy {
  PolicyId id;
  PolicyGeneration generation;
  ControlPlaneEpoch epoch;
  Timestamp effective_from;
  /// Evidence older than this at the accounting instant is stale. Zero means
  /// evidence never ages out.
  DurationMs max_evidence_age;
  /// When true, a contribution declared OutOfService must name evidence for
  /// it; otherwise its disposition is indeterminate rather than unavailable.
  bool require_out_of_service_evidence = true;
  /// When true, every contribution must state its contribution class.
  bool require_classification = true;
  /// When true, a Known installed quantity must name primary evidence.
  bool require_installed_evidence = true;
  /// When true, a degraded contribution must name evidence for each derate.
  bool require_derate_evidence = true;
  /// Required share of the accounted installed mass that must be determinate
  /// for a scope to be considered covered.
  Ratio minimum_coverage = Ratio::one();
  /// Residual magnitude, as a share of accounted installed mass, above which
  /// the unexplained residual is an error rather than a warning.
  Ratio residual_tolerance = Ratio::none();
  /// Upper bound on derate factors per contribution, never above the hard
  /// limit.
  std::size_t max_derate_factors = kMaxDerateFactors;
  /// Number of published accounting generations the store keeps.
  std::size_t retention_generations = kMaxRetainedGenerations;
  /// Free-form policy note, rendered in explanations.
  BoundedText note;

  [[nodiscard]] static AccountingPolicy baseline();
  [[nodiscard]] Result<void> validate(const Limits& limits) const;
  /// Digest of every policy field; the policy generation is only meaningful
  /// together with it.
  [[nodiscard]] Digest digest() const;
  /// Stable one-line summary of the policy.
  [[nodiscard]] std::string to_string() const;
};

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_POLICY_HPP
