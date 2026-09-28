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

#ifndef COOLING_CAPACITY_ACCOUNTING_TESTS_FIXTURES_HPP
#define COOLING_CAPACITY_ACCOUNTING_TESTS_FIXTURES_HPP

// SYNTHETIC facility data. Nothing in this file was measured on real cooling
// equipment: it exists so that the accounting semantics can be exercised.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

namespace cca_test {

/// Builds a validated identifier from a literal the test controls.
template <typename Id>
[[nodiscard]] Id id_of(std::string_view text) {
  return Id::from_validated(
      cooling_capacity_accounting::Identifier::from_validated(std::string(text)));
}

using cooling_capacity_accounting::AccountingInput;
using cooling_capacity_accounting::AccountingPolicy;
using cooling_capacity_accounting::Contribution;
using cooling_capacity_accounting::ContributionClass;
using cooling_capacity_accounting::ContributionGroup;
using cooling_capacity_accounting::DurationMs;
using cooling_capacity_accounting::EvidenceBinding;
using cooling_capacity_accounting::EvidenceId;
using cooling_capacity_accounting::EvidenceKind;
using cooling_capacity_accounting::EvidenceRecord;
using cooling_capacity_accounting::EvidenceSourceKind;
using cooling_capacity_accounting::GenerationBundle;
using cooling_capacity_accounting::Measure;
using cooling_capacity_accounting::Medium;
using cooling_capacity_accounting::Ratio;
using cooling_capacity_accounting::ServiceState;
using cooling_capacity_accounting::ThermalPower;
using cooling_capacity_accounting::Timestamp;

/// The instant every fixture is accounted at, so freshness behaviour is exact.
[[nodiscard]] Timestamp fixture_now();

/// An instant exactly this many milliseconds before fixture_now().
[[nodiscard]] Timestamp fixture_ago(std::int64_t milliseconds);

/// One evidence record with all required fields filled in.
[[nodiscard]] EvidenceRecord make_evidence(std::string_view id, EvidenceKind kind,
                                           Timestamp observed_at,
                                           GenerationBundle generations);

/// A minimal policy whose evidentiary requirements are relaxed enough for the
/// small fixtures, with every relaxation stated explicitly.
[[nodiscard]] AccountingPolicy relaxed_policy();

/// The strict baseline policy.
[[nodiscard]] AccountingPolicy strict_policy();

/// A single additive contribution in a loop scope, fully evidenced.
[[nodiscard]] Contribution make_contribution(std::string_view id, std::string_view scope,
                                             std::string_view klass,
                                             std::string_view equipment,
                                             std::int64_t installed_mw,
                                             GenerationBundle generations);

struct FacilitySpec {
  std::size_t zones = 2;
  std::size_t loops_per_zone = 2;
  std::size_t units_per_loop = 3;
  std::int64_t unit_installed_mw = 250'000;
  GenerationBundle generations = GenerationBundle::initial();
  AccountingPolicy policy = relaxed_policy();
  Timestamp observed_at = fixture_now();
};

/// Builds a synthetic site/zone/loop/unit facility with one evidence record per
/// contribution and one equipment class per medium.
[[nodiscard]] AccountingInput make_facility(const FacilitySpec& spec);

/// Convenience: the site scope id used by make_facility.
[[nodiscard]] std::string facility_site_id();
/// Convenience: the zone scope id for a zone index.
[[nodiscard]] std::string facility_zone_id(std::size_t zone);
/// Convenience: the loop scope id for a loop index.
[[nodiscard]] std::string facility_loop_id(std::size_t zone, std::size_t loop);
/// Convenience: the contribution id for one unit.
[[nodiscard]] std::string facility_unit_id(std::size_t zone, std::size_t loop,
                                           std::size_t unit);
/// Convenience: the air equipment class id used by make_facility.
[[nodiscard]] std::string facility_air_class_id();

/// A 128-bit identifier from its 32 hexadecimal characters.
[[nodiscard]] cooling_capacity_accounting::AttemptId attempt_of(std::string_view hex32);

/// The equipment class the property tests build their facilities around.
[[nodiscard]] std::string property_class_id();
[[nodiscard]] cooling_capacity_accounting::EquipmentClass make_class_for_property_tests();
[[nodiscard]] cooling_capacity_accounting::AccountingScope make_scope_for_property_tests(
    std::string_view id, cooling_capacity_accounting::ScopeKind kind,
    std::string_view parent, cooling_capacity_accounting::Medium medium);

}  // namespace cca_test

#endif  // COOLING_CAPACITY_ACCOUNTING_TESTS_FIXTURES_HPP
