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

#include "fixtures.hpp"

#include <string>
#include <utility>
#include <vector>

namespace cca_test {
namespace {

using cooling_capacity_accounting::AccountingScope;
using cooling_capacity_accounting::EquipmentClass;
using cooling_capacity_accounting::AttemptId;
using cooling_capacity_accounting::EquipmentClassId;
using cooling_capacity_accounting::EquipmentClassKind;
using cooling_capacity_accounting::ScopeId;
using cooling_capacity_accounting::ScopeKind;

constexpr std::int64_t kFixtureNowMilliseconds = 1'767'225'600'000LL;  // 2026-01-01

}  // namespace

Timestamp fixture_now() {
  return Timestamp::of_unix_milliseconds(kFixtureNowMilliseconds);
}

Timestamp fixture_ago(std::int64_t milliseconds) {
  return Timestamp::of_unix_milliseconds(kFixtureNowMilliseconds - milliseconds);
}

EvidenceRecord make_evidence(std::string_view id, EvidenceKind kind, Timestamp observed_at,
                             GenerationBundle generations) {
  EvidenceRecord record;
  record.id = id_of<EvidenceId>(id);
  record.kind = kind;
  record.source_kind = EvidenceSourceKind::SyntheticGenerator;
  record.source = id_of<cooling_capacity_accounting::EvidenceSourceId>("synthetic-source");
  record.label = cooling_capacity_accounting::BoundedText::from_validated(
      "synthetic fixture evidence");
  record.observed_at = observed_at;
  record.recorded_at = observed_at;
  record.binding = EvidenceBinding{generations.epoch, generations.topology,
                                   generations.policy, generations.evidence};
  record.medium = Medium::Air;
  record.content_digest = cooling_capacity_accounting::Digest::of_text(id);
  return record;
}

AccountingPolicy relaxed_policy() {
  AccountingPolicy policy = AccountingPolicy::baseline();
  policy.id = id_of<cooling_capacity_accounting::PolicyId>("cca.policy.relaxed");
  // The fixtures state a classification and a service state explicitly; the
  // policy relaxation is only about evidence, and it is stated here so no test
  // depends on hidden behaviour.
  policy.require_installed_evidence = false;
  policy.require_derate_evidence = false;
  policy.require_out_of_service_evidence = false;
  return policy;
}

AccountingPolicy strict_policy() {
  AccountingPolicy policy = AccountingPolicy::baseline();
  policy.id = id_of<cooling_capacity_accounting::PolicyId>("cca.policy.strict");
  return policy;
}

Contribution make_contribution(std::string_view id, std::string_view scope,
                               std::string_view klass, std::string_view equipment,
                               std::int64_t installed_mw, GenerationBundle generations) {
  Contribution contribution;
  contribution.id = id_of<cooling_capacity_accounting::ContributionId>(id);
  contribution.home_scope = id_of<ScopeId>(scope);
  contribution.equipment = id_of<cooling_capacity_accounting::EquipmentId>(equipment);
  contribution.equipment_class = id_of<EquipmentClassId>(klass);
  contribution.medium = Medium::Air;
  contribution.classification = ContributionClass::Additive;
  contribution.installed = Measure<ThermalPower>::known(
      ThermalPower::of_milliwatts(installed_mw));
  contribution.service = ServiceState::InService;
  contribution.binding = EvidenceBinding{generations.epoch, generations.topology,
                                         generations.policy, generations.evidence};
  contribution.observed_at = fixture_now();
  return contribution;
}

std::string facility_site_id() {
  return "site.alpha";
}

std::string facility_zone_id(std::size_t zone) {
  return "zone." + std::to_string(zone);
}

std::string facility_loop_id(std::size_t zone, std::size_t loop) {
  return "loop." + std::to_string(zone) + "." + std::to_string(loop);
}

std::string facility_unit_id(std::size_t zone, std::size_t loop, std::size_t unit) {
  return "unit." + std::to_string(zone) + "." + std::to_string(loop) + "." +
         std::to_string(unit);
}

std::string facility_air_class_id() {
  return "class.crah";
}

AttemptId attempt_of(std::string_view hex32) {
  const cooling_capacity_accounting::Result<AttemptId> parsed = AttemptId::parse(hex32);
  return parsed.ok() ? parsed.value() : AttemptId();
}

std::string property_class_id() {
  return "class.property";
}

EquipmentClass make_class_for_property_tests() {
  EquipmentClass klass;
  klass.id = id_of<EquipmentClassId>(property_class_id());
  klass.kind = EquipmentClassKind::ComputerRoomAirHandler;
  klass.medium = Medium::Air;
  klass.label = cooling_capacity_accounting::BoundedText::from_validated("property unit");
  klass.contributes_to_installed = true;
  return klass;
}

AccountingScope make_scope_for_property_tests(std::string_view id, ScopeKind kind,
                                             std::string_view parent, Medium medium) {
  AccountingScope scope;
  scope.id = id_of<ScopeId>(id);
  scope.kind = kind;
  if (!parent.empty()) {
    scope.parent = id_of<ScopeId>(parent);
  }
  scope.medium = medium;
  scope.label = cooling_capacity_accounting::BoundedText::from_validated(std::string(id));
  scope.topology_generation = cooling_capacity_accounting::TopologyGeneration::initial();
  scope.epoch = cooling_capacity_accounting::ControlPlaneEpoch::initial();
  return scope;
}

AccountingInput make_facility(const FacilitySpec& spec) {
  AccountingInput input;
  input.generations = spec.generations;
  input.policy = spec.policy;
  input.policy.generation = spec.generations.policy;
  input.policy.epoch = spec.generations.epoch;

  EquipmentClass klass;
  klass.id = id_of<EquipmentClassId>(facility_air_class_id());
  klass.kind = EquipmentClassKind::ComputerRoomAirHandler;
  klass.medium = Medium::Air;
  klass.label = cooling_capacity_accounting::BoundedText::from_validated("CRAC unit");
  input.equipment_classes.push_back(klass);

  AccountingScope site;
  site.id = id_of<ScopeId>(facility_site_id());
  site.kind = ScopeKind::Site;
  site.label = cooling_capacity_accounting::BoundedText::from_validated("synthetic site");
  site.medium = Medium::Air;
  site.topology_generation = spec.generations.topology;
  site.epoch = spec.generations.epoch;
  input.scopes.push_back(site);

  for (std::size_t zone = 0; zone < spec.zones; ++zone) {
    AccountingScope zone_scope;
    zone_scope.id = id_of<ScopeId>(facility_zone_id(zone));
    zone_scope.kind = ScopeKind::Zone;
    zone_scope.parent = id_of<ScopeId>(facility_site_id());
    zone_scope.label = cooling_capacity_accounting::BoundedText::from_validated(
        "synthetic zone " + std::to_string(zone));
    zone_scope.medium = Medium::Air;
    zone_scope.topology_generation = spec.generations.topology;
    zone_scope.epoch = spec.generations.epoch;
    input.scopes.push_back(zone_scope);

    for (std::size_t loop = 0; loop < spec.loops_per_zone; ++loop) {
      AccountingScope loop_scope;
      loop_scope.id = id_of<ScopeId>(facility_loop_id(zone, loop));
      loop_scope.kind = ScopeKind::Loop;
      loop_scope.parent = id_of<ScopeId>(facility_zone_id(zone));
      loop_scope.label = cooling_capacity_accounting::BoundedText::from_validated(
          "synthetic loop");
      loop_scope.medium = Medium::Air;
      loop_scope.topology_generation = spec.generations.topology;
      loop_scope.epoch = spec.generations.epoch;
      input.scopes.push_back(loop_scope);

      for (std::size_t unit = 0; unit < spec.units_per_loop; ++unit) {
        const std::string unit_name = facility_unit_id(zone, loop, unit);
        input.contributions.push_back(make_contribution(
            unit_name, facility_loop_id(zone, loop), facility_air_class_id(),
            "equipment." + unit_name, spec.unit_installed_mw, spec.generations));
      }
    }
  }
  return input;
}

}  // namespace cca_test
