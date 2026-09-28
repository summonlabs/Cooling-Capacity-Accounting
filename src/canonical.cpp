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

#include "cooling_capacity_accounting/canonical.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "byte_io.hpp"
#include "cooling_capacity_accounting/version.hpp"
#include "internal.hpp"

namespace cooling_capacity_accounting {
namespace {

using internal::ByteReader;
using internal::ByteWriter;

constexpr std::size_t kMagicSize = 8;
constexpr std::size_t kHeaderSize = kMagicSize + 4U + 4U + 8U;
constexpr std::string_view kMagic = "CCAIN001";
constexpr std::string_view kTrailer = "CCAEND01";

[[nodiscard]] bool is_defined_measure_state(MeasureState state) noexcept {
  return state == MeasureState::Known || state == MeasureState::Unknown ||
         state == MeasureState::Unsupported;
}

[[nodiscard]] bool is_defined_measure_reason(MeasureReason reason) noexcept {
  switch (reason) {
    case MeasureReason::None:
    case MeasureReason::NotMeasured:
    case MeasureReason::SensorAbsent:
    case MeasureReason::SensorFault:
    case MeasureReason::EvidenceMissing:
    case MeasureReason::EvidenceStale:
    case MeasureReason::EvidenceConflicting:
    case MeasureReason::EvidenceSuperseded:
    case MeasureReason::TopologyUnknown:
    case MeasureReason::InventoryUnknown:
    case MeasureReason::VendorSilent:
    case MeasureReason::NotCommissioned:
    case MeasureReason::OutOfScope:
    case MeasureReason::Other:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_defined_medium(Medium medium) noexcept {
  return medium == Medium::Air || medium == Medium::Liquid;
}

[[nodiscard]] bool is_defined_scope_kind(ScopeKind kind) noexcept {
  switch (kind) {
    case ScopeKind::Site:
    case ScopeKind::Zone:
    case ScopeKind::Loop:
    case ScopeKind::ClassBand:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_defined_class_kind(EquipmentClassKind kind) noexcept {
  switch (kind) {
    case EquipmentClassKind::ComputerRoomAirHandler:
    case EquipmentClassKind::ComputerRoomAirConditioner:
    case EquipmentClassKind::Chiller:
    case EquipmentClassKind::CoolantDistributionUnit:
    case EquipmentClassKind::CoolingTower:
    case EquipmentClassKind::DryCooler:
    case EquipmentClassKind::Pump:
    case EquipmentClassKind::ImmersionTank:
    case EquipmentClassKind::RearDoorHeatExchanger:
    case EquipmentClassKind::Economizer:
    case EquipmentClassKind::Other:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_defined_evidence_kind(EvidenceKind kind) noexcept {
  switch (kind) {
    case EvidenceKind::Nameplate:
    case EvidenceKind::VendorDatasheet:
    case EvidenceKind::CommissioningReport:
    case EvidenceKind::CapacityTest:
    case EvidenceKind::SensorReading:
    case EvidenceKind::MaintenanceRecord:
    case EvidenceKind::OutOfServiceOrder:
    case EvidenceKind::WorkOrder:
    case EvidenceKind::OperatorDeclaration:
    case EvidenceKind::AggregatedManifest:
    case EvidenceKind::PolicyDeclaration:
    case EvidenceKind::DerivedCalculation:
    case EvidenceKind::TopologyDeclaration:
    case EvidenceKind::Unknown:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_defined_source_kind(EvidenceSourceKind kind) noexcept {
  switch (kind) {
    case EvidenceSourceKind::VendorDocument:
    case EvidenceSourceKind::CommissioningTool:
    case EvidenceSourceKind::BmsExport:
    case EvidenceSourceKind::DcimExport:
    case EvidenceSourceKind::Operator:
    case EvidenceSourceKind::TestRig:
    case EvidenceSourceKind::SyntheticGenerator:
    case EvidenceSourceKind::DerivedInRepository:
    case EvidenceSourceKind::Other:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_defined_redundancy_class(RedundancyClass klass) noexcept {
  switch (klass) {
    case RedundancyClass::N:
    case RedundancyClass::NPlus1:
    case RedundancyClass::NPlus2:
    case RedundancyClass::TwoN:
    case RedundancyClass::TwoNPlus1:
    case RedundancyClass::TwoNPlus2:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_defined_contribution_class(ContributionClass klass) noexcept {
  switch (klass) {
    case ContributionClass::Unspecified:
    case ContributionClass::Additive:
    case ContributionClass::Substitutive:
    case ContributionClass::Redundant:
    case ContributionClass::ReserveOnly:
    case ContributionClass::MutuallyExclusive:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_defined_service_state(ServiceState state) noexcept {
  switch (state) {
    case ServiceState::Unspecified:
    case ServiceState::InService:
    case ServiceState::OutOfService:
    case ServiceState::Degraded:
    case ServiceState::Unknown:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_defined_derate_kind(DerateKind kind) noexcept {
  return kind == DerateKind::Factor || kind == DerateKind::Absolute;
}

[[nodiscard]] bool is_defined_sharing_kind(Sharing::Kind kind) noexcept {
  return kind == Sharing::Kind::Exclusive || kind == Sharing::Kind::Apportioned;
}

void write_digest(ByteWriter& writer, const Digest& digest) {
  writer.write_raw(digest.bytes().data(), digest.bytes().size());
}

[[nodiscard]] Result<Digest> read_digest(ByteReader& reader) {
  CCA_TRY_ASSIGN(bytes, reader.read_raw(Digest::kSize));
  std::array<std::uint8_t, Digest::kSize> value{};
  std::copy(bytes.begin(), bytes.end(), value.begin());
  return Digest::from_bytes(value);
}

void write_binding(ByteWriter& writer, const EvidenceBinding& binding) {
  writer.write_u64(binding.epoch.value());
  writer.write_u64(binding.topology.value());
  writer.write_u64(binding.policy.value());
  writer.write_u64(binding.evidence.value());
}

[[nodiscard]] Result<EvidenceBinding> read_binding(ByteReader& reader) {
  EvidenceBinding binding;
  CCA_TRY_ASSIGN(epoch, reader.read_u64());
  CCA_TRY_ASSIGN(topology, reader.read_u64());
  CCA_TRY_ASSIGN(policy, reader.read_u64());
  CCA_TRY_ASSIGN(evidence, reader.read_u64());
  CCA_TRY_ASSIGN(epoch_value, ControlPlaneEpoch::of(epoch));
  CCA_TRY_ASSIGN(topology_value, TopologyGeneration::of(topology));
  CCA_TRY_ASSIGN(policy_value, PolicyGeneration::of(policy));
  CCA_TRY_ASSIGN(evidence_value, EvidenceGeneration::of(evidence));
  binding.epoch = epoch_value;
  binding.topology = topology_value;
  binding.policy = policy_value;
  binding.evidence = evidence_value;
  return binding;
}

void write_generations(ByteWriter& writer, const GenerationBundle& generations) {
  writer.write_u64(generations.epoch.value());
  writer.write_u64(generations.topology.value());
  writer.write_u64(generations.policy.value());
  writer.write_u64(generations.evidence.value());
  writer.write_u64(generations.revision.value());
}

[[nodiscard]] Result<GenerationBundle> read_generations(ByteReader& reader) {
  GenerationBundle bundle;
  CCA_TRY_ASSIGN(epoch, reader.read_u64());
  CCA_TRY_ASSIGN(topology, reader.read_u64());
  CCA_TRY_ASSIGN(policy, reader.read_u64());
  CCA_TRY_ASSIGN(evidence, reader.read_u64());
  CCA_TRY_ASSIGN(revision, reader.read_u64());
  CCA_TRY_ASSIGN(epoch_value, ControlPlaneEpoch::of(epoch));
  CCA_TRY_ASSIGN(topology_value, TopologyGeneration::of(topology));
  CCA_TRY_ASSIGN(policy_value, PolicyGeneration::of(policy));
  CCA_TRY_ASSIGN(evidence_value, EvidenceGeneration::of(evidence));
  CCA_TRY_ASSIGN(revision_value, StateRevision::of(revision));
  bundle.epoch = epoch_value;
  bundle.topology = topology_value;
  bundle.policy = policy_value;
  bundle.evidence = evidence_value;
  bundle.revision = revision_value;
  return bundle;
}

void write_measure_power(ByteWriter& writer, const Measure<ThermalPower>& measure) {
  writer.write_u32(static_cast<std::uint32_t>(measure.state()));
  writer.write_u32(static_cast<std::uint32_t>(measure.reason()));
  writer.write_text(measure.explanation().view());
  writer.write_i64(measure.is_known() ? measure.value().milliwatts() : 0);
}

[[nodiscard]] Result<Measure<ThermalPower>> read_measure_power(ByteReader& reader) {
  CCA_TRY_ASSIGN(state_raw, reader.read_u32());
  CCA_TRY_ASSIGN(reason_raw, reader.read_u32());
  CCA_TRY_ASSIGN(explanation_text, reader.read_text(kMaxTextLength));
  CCA_TRY_ASSIGN(value, reader.read_i64());
  CCA_TRY_ASSIGN(state, internal::decode_enum<MeasureState>(
                            state_raw, is_defined_measure_state, "measure state"));
  CCA_TRY_ASSIGN(reason, internal::decode_enum<MeasureReason>(
                             reason_raw, is_defined_measure_reason, "measure reason"));
  CCA_TRY_ASSIGN(explanation, BoundedText::parse(explanation_text));
  if (state == MeasureState::Known) {
    if (reason != MeasureReason::None) {
      return Error::of(ErrorCode::MalformedRecord,
                       "a known measure carries a not-known reason");
    }
    CCA_TRY_ASSIGN(quantity, ThermalPower::checked_milliwatts(value));
    return Measure<ThermalPower>::known(quantity);
  }
  // A measure that is not known carries no quantity. A non-zero one here is a
  // field the encoder would never write, so accepting it would produce bytes
  // that do not round trip.
  if (value != 0) {
    return Error::of(ErrorCode::ReservedFieldNotZero,
                     "a measure that is not known carries a non-zero quantity")
        .with_detail("value " + to_decimal(value));
  }
  if (state == MeasureState::Unknown) {
    return Measure<ThermalPower>::unknown(reason, explanation);
  }
  return Measure<ThermalPower>::unsupported(reason, explanation);
}

void write_evidence(ByteWriter& writer, const EvidenceRecord& record) {
  writer.write_text(record.id.view());
  writer.write_u32(static_cast<std::uint32_t>(record.kind));
  writer.write_u32(static_cast<std::uint32_t>(record.source_kind));
  writer.write_text(record.source.view());
  writer.write_text(record.label.view());
  writer.write_text(record.reference.view());
  writer.write_i64(record.observed_at.unix_milliseconds());
  writer.write_i64(record.recorded_at.unix_milliseconds());
  writer.write_u64(record.sequence.value());
  write_binding(writer, record.binding);
  write_digest(writer, record.content_digest);
  writer.write_u32(static_cast<std::uint32_t>(record.medium));
  writer.write_optional_text(record.subject_equipment.has_value(),
                             record.subject_equipment.has_value()
                                 ? record.subject_equipment->view()
                                 : std::string_view());
  writer.write_optional_text(record.subject_scope.has_value(),
                             record.subject_scope.has_value()
                                 ? record.subject_scope->view()
                                 : std::string_view());
  write_measure_power(writer, record.declared_value);
  writer.write_text(record.notes.view());
}

[[nodiscard]] Result<EvidenceRecord> read_evidence(ByteReader& reader) {
  EvidenceRecord record;
  CCA_TRY_ASSIGN(id_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(id, EvidenceId::parse(id_text));
  record.id = id;
  CCA_TRY_ASSIGN(kind_raw, reader.read_u32());
  CCA_TRY_ASSIGN(kind, internal::decode_enum<EvidenceKind>(
                           kind_raw, is_defined_evidence_kind, "evidence kind"));
  record.kind = kind;
  CCA_TRY_ASSIGN(source_kind_raw, reader.read_u32());
  CCA_TRY_ASSIGN(source_kind, internal::decode_enum<EvidenceSourceKind>(
                                  source_kind_raw, is_defined_source_kind,
                                  "evidence source kind"));
  record.source_kind = source_kind;
  CCA_TRY_ASSIGN(source_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(source, EvidenceSourceId::parse(source_text));
  record.source = source;
  CCA_TRY_ASSIGN(label_text, reader.read_text(kMaxTextLength));
  CCA_TRY_ASSIGN(label, BoundedText::parse(label_text));
  record.label = label;
  CCA_TRY_ASSIGN(reference_text, reader.read_text(kMaxReferenceLength));
  if (!reference_text.empty()) {
    CCA_TRY_ASSIGN(reference, DocumentRef::parse(reference_text));
    record.reference = reference;
  }
  CCA_TRY_ASSIGN(observed_at, reader.read_i64());
  CCA_TRY_ASSIGN(observed_at_value, Timestamp::checked_unix_milliseconds(observed_at));
  record.observed_at = observed_at_value;
  CCA_TRY_ASSIGN(recorded_at, reader.read_i64());
  CCA_TRY_ASSIGN(recorded_at_value, Timestamp::checked_unix_milliseconds(recorded_at));
  record.recorded_at = recorded_at_value;
  CCA_TRY_ASSIGN(sequence, reader.read_u64());
  CCA_TRY_ASSIGN(sequence_value, ObservationSequence::of(sequence));
  record.sequence = sequence_value;
  CCA_TRY_ASSIGN(binding, read_binding(reader));
  record.binding = binding;
  CCA_TRY_ASSIGN(digest, read_digest(reader));
  record.content_digest = digest;
  CCA_TRY_ASSIGN(medium_raw, reader.read_u32());
  CCA_TRY_ASSIGN(medium, internal::decode_enum<Medium>(medium_raw, is_defined_medium,
                                                       "medium"));
  record.medium = medium;
  CCA_TRY_ASSIGN(subject_equipment_text,
                 reader.read_optional_text(kMaxIdentifierLength));
  if (!subject_equipment_text.empty()) {
    CCA_TRY_ASSIGN(subject_equipment, EquipmentId::parse(subject_equipment_text));
    record.subject_equipment = subject_equipment;
  }
  CCA_TRY_ASSIGN(subject_scope_text, reader.read_optional_text(kMaxIdentifierLength));
  if (!subject_scope_text.empty()) {
    CCA_TRY_ASSIGN(subject_scope, ScopeId::parse(subject_scope_text));
    record.subject_scope = subject_scope;
  }
  CCA_TRY_ASSIGN(declared_value, read_measure_power(reader));
  record.declared_value = declared_value;
  CCA_TRY_ASSIGN(notes_text, reader.read_text(kMaxTextLength));
  CCA_TRY_ASSIGN(notes, BoundedText::parse(notes_text));
  record.notes = notes;
  return record;
}

void write_class(ByteWriter& writer, const EquipmentClass& klass) {
  writer.write_text(klass.id.view());
  writer.write_u32(static_cast<std::uint32_t>(klass.kind));
  writer.write_u32(static_cast<std::uint32_t>(klass.medium));
  writer.write_text(klass.label.view());
  writer.write_bool(klass.contributes_to_installed);
}

[[nodiscard]] Result<EquipmentClass> read_class(ByteReader& reader) {
  EquipmentClass klass;
  CCA_TRY_ASSIGN(id_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(id, EquipmentClassId::parse(id_text));
  klass.id = id;
  CCA_TRY_ASSIGN(kind_raw, reader.read_u32());
  CCA_TRY_ASSIGN(kind, internal::decode_enum<EquipmentClassKind>(
                           kind_raw, is_defined_class_kind, "equipment class kind"));
  klass.kind = kind;
  CCA_TRY_ASSIGN(medium_raw, reader.read_u32());
  CCA_TRY_ASSIGN(medium, internal::decode_enum<Medium>(medium_raw, is_defined_medium,
                                                       "medium"));
  klass.medium = medium;
  CCA_TRY_ASSIGN(label_text, reader.read_text(kMaxTextLength));
  CCA_TRY_ASSIGN(label, BoundedText::parse(label_text));
  klass.label = label;
  CCA_TRY_ASSIGN(contributes, reader.read_bool());
  klass.contributes_to_installed = contributes;
  return klass;
}

void write_domain(ByteWriter& writer, const IndependenceDomain& domain) {
  writer.write_text(domain.id.view());
  writer.write_text(domain.label.view());
  writer.write_text(domain.evidence.view());
  writer.write_text(domain.scope.view());
  writer.write_u64(domain.topology_generation.value());
  writer.write_u64(domain.epoch.value());
}

[[nodiscard]] Result<IndependenceDomain> read_domain(ByteReader& reader) {
  IndependenceDomain domain;
  CCA_TRY_ASSIGN(id_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(id, IndependenceDomainId::parse(id_text));
  domain.id = id;
  CCA_TRY_ASSIGN(label_text, reader.read_text(kMaxTextLength));
  CCA_TRY_ASSIGN(label, BoundedText::parse(label_text));
  domain.label = label;
  CCA_TRY_ASSIGN(evidence_text, reader.read_text(kMaxIdentifierLength));
  if (!evidence_text.empty()) {
    CCA_TRY_ASSIGN(evidence, EvidenceId::parse(evidence_text));
    domain.evidence = evidence;
  }
  CCA_TRY_ASSIGN(scope_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(scope, ScopeId::parse(scope_text));
  domain.scope = scope;
  CCA_TRY_ASSIGN(topology, reader.read_u64());
  CCA_TRY_ASSIGN(topology_value, TopologyGeneration::of(topology));
  domain.topology_generation = topology_value;
  CCA_TRY_ASSIGN(epoch, reader.read_u64());
  CCA_TRY_ASSIGN(epoch_value, ControlPlaneEpoch::of(epoch));
  domain.epoch = epoch_value;
  return domain;
}

void write_scope(ByteWriter& writer, const AccountingScope& scope) {
  writer.write_text(scope.id.view());
  writer.write_u32(static_cast<std::uint32_t>(scope.kind));
  writer.write_optional_text(scope.parent.has_value(),
                             scope.parent.has_value() ? scope.parent->view()
                                                      : std::string_view());
  writer.write_text(scope.label.view());
  writer.write_u32(static_cast<std::uint32_t>(scope.medium));
  writer.write_u32(static_cast<std::uint32_t>(scope.classes.size()));
  for (const EquipmentClassId& klass : scope.classes) {
    writer.write_text(klass.view());
  }
  writer.write_u64(scope.topology_generation.value());
  writer.write_u64(scope.epoch.value());
  write_measure_power(writer, scope.declared_installed_total);
  writer.write_u32(static_cast<std::uint32_t>(scope.declared_total_evidence.size()));
  for (const EvidenceId& evidence : scope.declared_total_evidence) {
    writer.write_text(evidence.view());
  }
}

[[nodiscard]] Result<AccountingScope> read_scope(ByteReader& reader,
                                                 const Limits& limits) {
  AccountingScope scope;
  CCA_TRY_ASSIGN(id_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(id, ScopeId::parse(id_text));
  scope.id = id;
  CCA_TRY_ASSIGN(kind_raw, reader.read_u32());
  CCA_TRY_ASSIGN(kind, internal::decode_enum<ScopeKind>(kind_raw, is_defined_scope_kind,
                                                        "scope kind"));
  scope.kind = kind;
  CCA_TRY_ASSIGN(parent_text, reader.read_optional_text(kMaxIdentifierLength));
  if (!parent_text.empty()) {
    CCA_TRY_ASSIGN(parent, ScopeId::parse(parent_text));
    scope.parent = parent;
  }
  CCA_TRY_ASSIGN(label_text, reader.read_text(kMaxTextLength));
  CCA_TRY_ASSIGN(label, BoundedText::parse(label_text));
  scope.label = label;
  CCA_TRY_ASSIGN(medium_raw, reader.read_u32());
  CCA_TRY_ASSIGN(medium, internal::decode_enum<Medium>(medium_raw, is_defined_medium,
                                                       "medium"));
  scope.medium = medium;
  CCA_TRY_ASSIGN(class_count,
                 reader.read_count(limits.max_scopes, "declared classes"));
  for (std::uint32_t index = 0; index < class_count; ++index) {
    CCA_TRY_ASSIGN(class_text, reader.read_text(kMaxIdentifierLength));
    CCA_TRY_ASSIGN(class_id, EquipmentClassId::parse(class_text));
    scope.classes.push_back(class_id);
  }
  CCA_TRY_ASSIGN(topology, reader.read_u64());
  CCA_TRY_ASSIGN(topology_value, TopologyGeneration::of(topology));
  scope.topology_generation = topology_value;
  CCA_TRY_ASSIGN(epoch, reader.read_u64());
  CCA_TRY_ASSIGN(epoch_value, ControlPlaneEpoch::of(epoch));
  scope.epoch = epoch_value;
  CCA_TRY_ASSIGN(declared, read_measure_power(reader));
  scope.declared_installed_total = declared;
  CCA_TRY_ASSIGN(evidence_count, reader.read_count(kMaxEvidencePerContribution,
                                                   "declared-total evidence"));
  for (std::uint32_t index = 0; index < evidence_count; ++index) {
    CCA_TRY_ASSIGN(evidence_text, reader.read_text(kMaxIdentifierLength));
    CCA_TRY_ASSIGN(evidence_id, EvidenceId::parse(evidence_text));
    scope.declared_total_evidence.push_back(evidence_id);
  }
  return scope;
}

void write_group(ByteWriter& writer, const ContributionGroup& group) {
  writer.write_text(group.id.view());
  writer.write_text(group.scope.view());
  writer.write_u32(static_cast<std::uint32_t>(group.classification));
  writer.write_u32(static_cast<std::uint32_t>(group.medium));
  writer.write_u32(group.required_concurrent);
  writer.write_u32(static_cast<std::uint32_t>(group.redundancy));
  write_measure_power(writer, group.protected_quantity);
  writer.write_text(group.protected_quantity_evidence.view());
  writer.write_u32(static_cast<std::uint32_t>(group.independence_domains.size()));
  for (const IndependenceDomainId& domain : group.independence_domains) {
    writer.write_text(domain.view());
  }
  write_binding(writer, group.binding);
}

[[nodiscard]] Result<ContributionGroup> read_group(ByteReader& reader) {
  ContributionGroup group;
  CCA_TRY_ASSIGN(id_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(id, ContributionGroupId::parse(id_text));
  group.id = id;
  CCA_TRY_ASSIGN(scope_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(scope, ScopeId::parse(scope_text));
  group.scope = scope;
  CCA_TRY_ASSIGN(classification_raw, reader.read_u32());
  CCA_TRY_ASSIGN(classification,
                 internal::decode_enum<ContributionClass>(
                     classification_raw, is_defined_contribution_class,
                     "contribution class"));
  group.classification = classification;
  CCA_TRY_ASSIGN(medium_raw, reader.read_u32());
  CCA_TRY_ASSIGN(medium, internal::decode_enum<Medium>(medium_raw, is_defined_medium,
                                                       "medium"));
  group.medium = medium;
  CCA_TRY_ASSIGN(required_concurrent, reader.read_u32());
  group.required_concurrent = required_concurrent;
  CCA_TRY_ASSIGN(redundancy_raw, reader.read_u32());
  CCA_TRY_ASSIGN(redundancy, internal::decode_enum<RedundancyClass>(
                                 redundancy_raw, is_defined_redundancy_class,
                                 "redundancy class"));
  group.redundancy = redundancy;
  CCA_TRY_ASSIGN(protected_quantity, read_measure_power(reader));
  group.protected_quantity = protected_quantity;
  CCA_TRY_ASSIGN(protected_evidence_text, reader.read_text(kMaxIdentifierLength));
  if (!protected_evidence_text.empty()) {
    CCA_TRY_ASSIGN(protected_evidence, EvidenceId::parse(protected_evidence_text));
    group.protected_quantity_evidence = protected_evidence;
  }
  CCA_TRY_ASSIGN(domain_count, reader.read_count(kMaxIndependenceDomains,
                                                 "shared-fate domains"));
  for (std::uint32_t index = 0; index < domain_count; ++index) {
    CCA_TRY_ASSIGN(domain_text, reader.read_text(kMaxIdentifierLength));
    CCA_TRY_ASSIGN(domain_id, IndependenceDomainId::parse(domain_text));
    group.independence_domains.push_back(domain_id);
  }
  CCA_TRY_ASSIGN(binding, read_binding(reader));
  group.binding = binding;
  return group;
}

void write_derate(ByteWriter& writer, const DerateFactor& derate) {
  writer.write_text(derate.id.view());
  writer.write_u32(static_cast<std::uint32_t>(derate.kind));
  writer.write_u32(derate.factor.ppm());
  writer.write_i64(derate.absolute.milliwatts());
  writer.write_text(derate.evidence.view());
  writer.write_text(derate.label.view());
}

[[nodiscard]] Result<DerateFactor> read_derate(ByteReader& reader) {
  DerateFactor derate;
  CCA_TRY_ASSIGN(id_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(id, DerateId::parse(id_text));
  derate.id = id;
  CCA_TRY_ASSIGN(kind_raw, reader.read_u32());
  CCA_TRY_ASSIGN(kind, internal::decode_enum<DerateKind>(kind_raw, is_defined_derate_kind,
                                                         "derate kind"));
  derate.kind = kind;
  CCA_TRY_ASSIGN(factor_ppm, reader.read_u32());
  CCA_TRY_ASSIGN(factor, Ratio::of_ppm(factor_ppm));
  derate.factor = factor;
  CCA_TRY_ASSIGN(absolute, reader.read_i64());
  CCA_TRY_ASSIGN(absolute_value, ThermalPower::checked_milliwatts(absolute));
  derate.absolute = absolute_value;
  CCA_TRY_ASSIGN(evidence_text, reader.read_text(kMaxIdentifierLength));
  if (!evidence_text.empty()) {
    CCA_TRY_ASSIGN(evidence, EvidenceId::parse(evidence_text));
    derate.evidence = evidence;
  }
  CCA_TRY_ASSIGN(label_text, reader.read_text(kMaxTextLength));
  CCA_TRY_ASSIGN(label, BoundedText::parse(label_text));
  derate.label = label;
  return derate;
}

void write_contribution(ByteWriter& writer, const Contribution& contribution) {
  writer.write_text(contribution.id.view());
  writer.write_text(contribution.home_scope.view());
  writer.write_text(contribution.equipment.view());
  writer.write_text(contribution.equipment_class.view());
  writer.write_u32(static_cast<std::uint32_t>(contribution.medium));
  writer.write_u32(static_cast<std::uint32_t>(contribution.classification));
  write_measure_power(writer, contribution.installed);
  writer.write_u32(static_cast<std::uint32_t>(contribution.service));
  writer.write_u32(static_cast<std::uint32_t>(contribution.derates.size()));
  for (const DerateFactor& derate : contribution.derates) {
    write_derate(writer, derate);
  }
  writer.write_u32(contribution.priority);
  writer.write_u32(static_cast<std::uint32_t>(contribution.sharing.kind));
  writer.write_u32(static_cast<std::uint32_t>(contribution.sharing.shares.size()));
  for (const ApportionmentShare& share : contribution.sharing.shares) {
    writer.write_text(share.target.view());
    writer.write_u32(share.share.ppm());
  }
  writer.write_optional_text(
      contribution.group.has_value(),
      contribution.group.has_value() ? contribution.group->view() : std::string_view());
  writer.write_optional_text(contribution.independence_domain.has_value(),
                             contribution.independence_domain.has_value()
                                 ? contribution.independence_domain->view()
                                 : std::string_view());
  writer.write_u32(static_cast<std::uint32_t>(contribution.aliases.size()));
  for (const ScopeId& alias : contribution.aliases) {
    writer.write_text(alias.view());
  }
  write_binding(writer, contribution.binding);
  writer.write_text(contribution.primary_evidence.view());
  writer.write_u32(static_cast<std::uint32_t>(contribution.evidence.size()));
  for (const EvidenceId& evidence : contribution.evidence) {
    writer.write_text(evidence.view());
  }
  writer.write_i64(contribution.observed_at.unix_milliseconds());
  writer.write_u64(contribution.observation_sequence.value());
}

[[nodiscard]] Result<Contribution> read_contribution(ByteReader& reader) {
  Contribution contribution;
  CCA_TRY_ASSIGN(id_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(id, ContributionId::parse(id_text));
  contribution.id = id;
  CCA_TRY_ASSIGN(scope_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(scope, ScopeId::parse(scope_text));
  contribution.home_scope = scope;
  CCA_TRY_ASSIGN(equipment_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(equipment, EquipmentId::parse(equipment_text));
  contribution.equipment = equipment;
  CCA_TRY_ASSIGN(class_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(class_id, EquipmentClassId::parse(class_text));
  contribution.equipment_class = class_id;
  CCA_TRY_ASSIGN(medium_raw, reader.read_u32());
  CCA_TRY_ASSIGN(medium, internal::decode_enum<Medium>(medium_raw, is_defined_medium,
                                                       "medium"));
  contribution.medium = medium;
  CCA_TRY_ASSIGN(classification_raw, reader.read_u32());
  CCA_TRY_ASSIGN(classification,
                 internal::decode_enum<ContributionClass>(
                     classification_raw, is_defined_contribution_class,
                     "contribution class"));
  contribution.classification = classification;
  CCA_TRY_ASSIGN(installed, read_measure_power(reader));
  contribution.installed = installed;
  CCA_TRY_ASSIGN(service_raw, reader.read_u32());
  CCA_TRY_ASSIGN(service, internal::decode_enum<ServiceState>(
                              service_raw, is_defined_service_state, "service state"));
  contribution.service = service;
  CCA_TRY_ASSIGN(derate_count,
                 reader.read_count(kMaxDerateFactors, "derates"));
  for (std::uint32_t index = 0; index < derate_count; ++index) {
    CCA_TRY_ASSIGN(derate, read_derate(reader));
    contribution.derates.push_back(derate);
  }
  CCA_TRY_ASSIGN(priority, reader.read_u32());
  contribution.priority = priority;
  CCA_TRY_ASSIGN(sharing_raw, reader.read_u32());
  CCA_TRY_ASSIGN(sharing, internal::decode_enum<Sharing::Kind>(
                              sharing_raw, is_defined_sharing_kind, "sharing kind"));
  contribution.sharing.kind = sharing;
  CCA_TRY_ASSIGN(share_count, reader.read_count(kMaxApportionTargets,
                                                "apportionment shares"));
  for (std::uint32_t index = 0; index < share_count; ++index) {
    ApportionmentShare share;
    CCA_TRY_ASSIGN(target_text, reader.read_text(kMaxIdentifierLength));
    CCA_TRY_ASSIGN(target, ScopeId::parse(target_text));
    share.target = target;
    CCA_TRY_ASSIGN(ppm, reader.read_u32());
    CCA_TRY_ASSIGN(ratio, Ratio::of_ppm(ppm));
    share.share = ratio;
    contribution.sharing.shares.push_back(share);
  }
  CCA_TRY_ASSIGN(group_text, reader.read_optional_text(kMaxIdentifierLength));
  if (!group_text.empty()) {
    CCA_TRY_ASSIGN(group, ContributionGroupId::parse(group_text));
    contribution.group = group;
  }
  CCA_TRY_ASSIGN(domain_text, reader.read_optional_text(kMaxIdentifierLength));
  if (!domain_text.empty()) {
    CCA_TRY_ASSIGN(domain, IndependenceDomainId::parse(domain_text));
    contribution.independence_domain = domain;
  }
  CCA_TRY_ASSIGN(alias_count, reader.read_count(kMaxAliasesPerContribution, "aliases"));
  for (std::uint32_t index = 0; index < alias_count; ++index) {
    CCA_TRY_ASSIGN(alias_text, reader.read_text(kMaxIdentifierLength));
    CCA_TRY_ASSIGN(alias, ScopeId::parse(alias_text));
    contribution.aliases.push_back(alias);
  }
  CCA_TRY_ASSIGN(binding, read_binding(reader));
  contribution.binding = binding;
  CCA_TRY_ASSIGN(evidence_text, reader.read_text(kMaxIdentifierLength));
  if (!evidence_text.empty()) {
    CCA_TRY_ASSIGN(evidence, EvidenceId::parse(evidence_text));
    contribution.primary_evidence = evidence;
  }
  CCA_TRY_ASSIGN(evidence_count, reader.read_count(kMaxEvidencePerContribution,
                                                   "evidence references"));
  for (std::uint32_t index = 0; index < evidence_count; ++index) {
    CCA_TRY_ASSIGN(reference_text, reader.read_text(kMaxIdentifierLength));
    CCA_TRY_ASSIGN(reference, EvidenceId::parse(reference_text));
    contribution.evidence.push_back(reference);
  }
  CCA_TRY_ASSIGN(observed_at, reader.read_i64());
  CCA_TRY_ASSIGN(observed_at_value, Timestamp::checked_unix_milliseconds(observed_at));
  contribution.observed_at = observed_at_value;
  CCA_TRY_ASSIGN(sequence, reader.read_u64());
  CCA_TRY_ASSIGN(sequence_value, ObservationSequence::of(sequence));
  contribution.observation_sequence = sequence_value;
  return contribution;
}

void write_manifest(ByteWriter& writer, const ManifestDeclaration& declaration) {
  writer.write_text(declaration.id.view());
  writer.write_text(declaration.scope.view());
  writer.write_optional_text(declaration.equipment_class.has_value(),
                             declaration.equipment_class.has_value()
                                 ? declaration.equipment_class->view()
                                 : std::string_view());
  writer.write_u32(static_cast<std::uint32_t>(declaration.medium));
  writer.write_i64(declaration.declared_installed.milliwatts());
  writer.write_text(declaration.evidence.view());
  writer.write_i64(declaration.observed_at.unix_milliseconds());
  write_binding(writer, declaration.binding);
}

[[nodiscard]] Result<ManifestDeclaration> read_manifest(ByteReader& reader) {
  ManifestDeclaration declaration;
  CCA_TRY_ASSIGN(id_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(id, ManifestId::parse(id_text));
  declaration.id = id;
  CCA_TRY_ASSIGN(scope_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(scope, ScopeId::parse(scope_text));
  declaration.scope = scope;
  CCA_TRY_ASSIGN(class_text, reader.read_optional_text(kMaxIdentifierLength));
  if (!class_text.empty()) {
    CCA_TRY_ASSIGN(class_id, EquipmentClassId::parse(class_text));
    declaration.equipment_class = class_id;
  }
  CCA_TRY_ASSIGN(medium_raw, reader.read_u32());
  CCA_TRY_ASSIGN(medium, internal::decode_enum<Medium>(medium_raw, is_defined_medium,
                                                       "medium"));
  declaration.medium = medium;
  CCA_TRY_ASSIGN(declared, reader.read_i64());
  CCA_TRY_ASSIGN(declared_value, ThermalPower::checked_milliwatts(declared));
  declaration.declared_installed = declared_value;
  CCA_TRY_ASSIGN(evidence_text, reader.read_text(kMaxIdentifierLength));
  if (!evidence_text.empty()) {
    CCA_TRY_ASSIGN(evidence, EvidenceId::parse(evidence_text));
    declaration.evidence = evidence;
  }
  CCA_TRY_ASSIGN(observed_at, reader.read_i64());
  CCA_TRY_ASSIGN(observed_at_value, Timestamp::checked_unix_milliseconds(observed_at));
  declaration.observed_at = observed_at_value;
  CCA_TRY_ASSIGN(binding, read_binding(reader));
  declaration.binding = binding;
  return declaration;
}

void write_policy(ByteWriter& writer, const AccountingPolicy& policy) {
  writer.write_text(policy.id.view());
  writer.write_u64(policy.generation.value());
  writer.write_u64(policy.epoch.value());
  writer.write_i64(policy.effective_from.unix_milliseconds());
  writer.write_i64(policy.max_evidence_age.milliseconds());
  writer.write_bool(policy.require_out_of_service_evidence);
  writer.write_bool(policy.require_classification);
  writer.write_bool(policy.require_installed_evidence);
  writer.write_bool(policy.require_derate_evidence);
  writer.write_u32(policy.minimum_coverage.ppm());
  writer.write_u32(policy.residual_tolerance.ppm());
  writer.write_u64(static_cast<std::uint64_t>(policy.max_derate_factors));
  writer.write_u64(static_cast<std::uint64_t>(policy.retention_generations));
  writer.write_text(policy.note.view());
}

[[nodiscard]] Result<AccountingPolicy> read_policy(ByteReader& reader) {
  AccountingPolicy policy;
  CCA_TRY_ASSIGN(id_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(id, PolicyId::parse(id_text));
  policy.id = id;
  CCA_TRY_ASSIGN(generation, reader.read_u64());
  CCA_TRY_ASSIGN(generation_value, PolicyGeneration::of(generation));
  policy.generation = generation_value;
  CCA_TRY_ASSIGN(epoch, reader.read_u64());
  CCA_TRY_ASSIGN(epoch_value, ControlPlaneEpoch::of(epoch));
  policy.epoch = epoch_value;
  CCA_TRY_ASSIGN(effective_from, reader.read_i64());
  CCA_TRY_ASSIGN(effective_from_value,
                 Timestamp::checked_unix_milliseconds(effective_from));
  policy.effective_from = effective_from_value;
  CCA_TRY_ASSIGN(max_evidence_age, reader.read_i64());
  CCA_TRY_ASSIGN(max_evidence_age_value,
                 DurationMs::checked_milliseconds(max_evidence_age));
  policy.max_evidence_age = max_evidence_age_value;
  CCA_TRY_ASSIGN(out_of_service, reader.read_bool());
  policy.require_out_of_service_evidence = out_of_service;
  CCA_TRY_ASSIGN(classification, reader.read_bool());
  policy.require_classification = classification;
  CCA_TRY_ASSIGN(installed, reader.read_bool());
  policy.require_installed_evidence = installed;
  CCA_TRY_ASSIGN(derate, reader.read_bool());
  policy.require_derate_evidence = derate;
  CCA_TRY_ASSIGN(minimum_coverage, reader.read_u32());
  CCA_TRY_ASSIGN(minimum_coverage_value, Ratio::of_ppm(minimum_coverage));
  policy.minimum_coverage = minimum_coverage_value;
  CCA_TRY_ASSIGN(residual_tolerance, reader.read_u32());
  CCA_TRY_ASSIGN(residual_tolerance_value, Ratio::of_ppm(residual_tolerance));
  policy.residual_tolerance = residual_tolerance_value;
  CCA_TRY_ASSIGN(max_derate_factors, reader.read_u64());
  policy.max_derate_factors = static_cast<std::size_t>(max_derate_factors);
  CCA_TRY_ASSIGN(retention, reader.read_u64());
  policy.retention_generations = static_cast<std::size_t>(retention);
  CCA_TRY_ASSIGN(note_text, reader.read_text(kMaxTextLength));
  CCA_TRY_ASSIGN(note, BoundedText::parse(note_text));
  policy.note = note;
  return policy;
}

template <typename T, typename Id>
[[nodiscard]] bool strictly_ascending(const std::vector<T>& values, Id key) {
  for (std::size_t index = 1; index < values.size(); ++index) {
    if (!(key(values[index - 1]) < key(values[index]))) {
      return false;
    }
  }
  return true;
}

template <typename T, typename Key>
void sort_records(std::vector<T>& values, Key key) {
  std::sort(values.begin(), values.end(),
            [&key](const T& lhs, const T& rhs) { return key(lhs) < key(rhs); });
}

Result<void> encode_canonical_into(ByteWriter& writer, const AccountingInput& input,
                                   const Limits& limits) {
  CCA_TRY(limits.validate());
  if (input.scopes.size() > limits.max_scopes) {
    return Error::of(ErrorCode::LimitExceeded, "too many scopes to encode");
  }
  if (input.contributions.size() > limits.max_contributions) {
    return Error::of(ErrorCode::LimitExceeded, "too many contributions to encode");
  }
  if (input.evidence.size() > limits.max_evidence_records) {
    return Error::of(ErrorCode::LimitExceeded, "too many evidence records to encode");
  }
  if (input.independence_domains.size() > limits.max_independence_domains) {
    return Error::of(ErrorCode::LimitExceeded,
                     "too many shared-fate declarations to encode");
  }

  writer.write_raw(kMagic.data(), kMagic.size());
  writer.write_u32(kCanonicalFormatVersion);
  writer.write_u32(0);  // reserved
  const std::size_t length_offset = writer.size();
  writer.write_u64(0);  // patched below

  write_generations(writer, input.generations);
  write_policy(writer, input.policy);

  std::vector<EquipmentClass> classes = input.equipment_classes;
  sort_records(classes, [](const EquipmentClass& value) { return value.id; });
  writer.write_u32(static_cast<std::uint32_t>(classes.size()));
  for (const EquipmentClass& klass : classes) {
    write_class(writer, klass);
  }

  std::vector<EvidenceRecord> evidence = input.evidence;
  sort_records(evidence, [](const EvidenceRecord& value) { return value.id; });
  writer.write_u32(static_cast<std::uint32_t>(evidence.size()));
  for (const EvidenceRecord& record : evidence) {
    write_evidence(writer, record);
  }

  std::vector<IndependenceDomain> domains = input.independence_domains;
  sort_records(domains, [](const IndependenceDomain& value) { return value.id; });
  writer.write_u32(static_cast<std::uint32_t>(domains.size()));
  for (const IndependenceDomain& domain : domains) {
    write_domain(writer, domain);
  }

  std::vector<AccountingScope> scopes = input.scopes;
  sort_records(scopes, [](const AccountingScope& value) { return value.id; });
  writer.write_u32(static_cast<std::uint32_t>(scopes.size()));
  for (const AccountingScope& scope : scopes) {
    write_scope(writer, scope);
  }

  std::vector<ContributionGroup> groups = input.groups;
  sort_records(groups, [](const ContributionGroup& value) { return value.id; });
  writer.write_u32(static_cast<std::uint32_t>(groups.size()));
  for (const ContributionGroup& group : groups) {
    write_group(writer, group);
  }

  std::vector<Contribution> contributions = input.contributions;
  sort_records(contributions, [](const Contribution& value) { return value.id; });
  writer.write_u32(static_cast<std::uint32_t>(contributions.size()));
  for (Contribution& contribution : contributions) {
    sort_records(contribution.derates,
                 [](const DerateFactor& value) { return value.id; });
    sort_records(contribution.sharing.shares,
                 [](const ApportionmentShare& value) { return value.target; });
    write_contribution(writer, contribution);
  }

  std::vector<ManifestDeclaration> manifests = input.manifest_declarations;
  sort_records(manifests, [](const ManifestDeclaration& value) { return value.id; });
  writer.write_u32(static_cast<std::uint32_t>(manifests.size()));
  for (const ManifestDeclaration& declaration : manifests) {
    write_manifest(writer, declaration);
  }

  writer.write_raw(kTrailer.data(), kTrailer.size());
  const std::size_t payload_length = writer.size() - length_offset - 8U;
  writer.overwrite_u64(length_offset, static_cast<std::uint64_t>(payload_length));
  return Ok{};
}

[[nodiscard]] Result<AccountingInput> decode_payload(ByteReader& reader,
                                                     const Limits& limits) {
  AccountingInput input;
  CCA_TRY_ASSIGN(generations, read_generations(reader));
  input.generations = generations;
  CCA_TRY_ASSIGN(policy, read_policy(reader));
  input.policy = policy;

  CCA_TRY_ASSIGN(class_count, reader.read_count(limits.max_scopes, "equipment classes"));
  for (std::uint32_t index = 0; index < class_count; ++index) {
    CCA_TRY_ASSIGN(klass, read_class(reader));
    input.equipment_classes.push_back(klass);
  }
  CCA_TRY_ASSIGN(evidence_count,
                 reader.read_count(limits.max_evidence_records, "evidence records"));
  for (std::uint32_t index = 0; index < evidence_count; ++index) {
    CCA_TRY_ASSIGN(record, read_evidence(reader));
    input.evidence.push_back(record);
  }
  CCA_TRY_ASSIGN(domain_count, reader.read_count(limits.max_independence_domains,
                                                 "shared-fate declarations"));
  for (std::uint32_t index = 0; index < domain_count; ++index) {
    CCA_TRY_ASSIGN(domain, read_domain(reader));
    input.independence_domains.push_back(domain);
  }
  CCA_TRY_ASSIGN(scope_count, reader.read_count(limits.max_scopes, "scopes"));
  for (std::uint32_t index = 0; index < scope_count; ++index) {
    CCA_TRY_ASSIGN(scope, read_scope(reader, limits));
    input.scopes.push_back(scope);
  }
  CCA_TRY_ASSIGN(group_count, reader.read_count(limits.max_scopes, "contribution groups"));
  for (std::uint32_t index = 0; index < group_count; ++index) {
    CCA_TRY_ASSIGN(group, read_group(reader));
    input.groups.push_back(group);
  }
  CCA_TRY_ASSIGN(contribution_count,
                 reader.read_count(limits.max_contributions, "contributions"));
  for (std::uint32_t index = 0; index < contribution_count; ++index) {
    CCA_TRY_ASSIGN(contribution, read_contribution(reader));
    input.contributions.push_back(contribution);
  }
  CCA_TRY_ASSIGN(manifest_count, reader.read_count(limits.max_scopes,
                                                   "manifest declarations"));
  for (std::uint32_t index = 0; index < manifest_count; ++index) {
    CCA_TRY_ASSIGN(declaration, read_manifest(reader));
    input.manifest_declarations.push_back(declaration);
  }

  CCA_TRY_ASSIGN(trailer, reader.read_raw(kTrailer.size()));
  if (std::string_view(reinterpret_cast<const char*>(trailer.data()), trailer.size()) !=
      kTrailer) {
    return Error::of(ErrorCode::MalformedRecord,
                     "the canonical payload does not end with its trailer");
  }
  if (!reader.empty()) {
    return Error::of(ErrorCode::TrailingBytes,
                     "the canonical payload continues after its trailer")
        .with_detail(to_decimal(static_cast<std::uint64_t>(reader.remaining())) +
                     " bytes remain");
  }

  // The encoding is canonical: records appear in strictly ascending identity
  // order and a duplicate identity can never be produced by the encoder.
  if (!strictly_ascending(input.equipment_classes,
                          [](const EquipmentClass& value) { return value.id; }) ||
      !strictly_ascending(input.evidence,
                          [](const EvidenceRecord& value) { return value.id; }) ||
      !strictly_ascending(input.independence_domains,
                          [](const IndependenceDomain& value) { return value.id; }) ||
      !strictly_ascending(input.scopes,
                          [](const AccountingScope& value) { return value.id; }) ||
      !strictly_ascending(input.groups,
                          [](const ContributionGroup& value) { return value.id; }) ||
      !strictly_ascending(input.contributions,
                          [](const Contribution& value) { return value.id; }) ||
      !strictly_ascending(input.manifest_declarations,
                          [](const ManifestDeclaration& value) { return value.id; })) {
    return Error::of(ErrorCode::RecordOrderViolation,
                     "canonical records must appear in strictly ascending identity "
                     "order without duplicates");
  }
  for (const Contribution& contribution : input.contributions) {
    if (!strictly_ascending(contribution.derates,
                            [](const DerateFactor& value) { return value.id; }) ||
        !strictly_ascending(contribution.sharing.shares,
                            [](const ApportionmentShare& value) { return value.target; })) {
      return Error::of(ErrorCode::RecordOrderViolation,
                       "canonical derates and shares must appear in strictly "
                       "ascending identity order")
          .with_subject(contribution.id.str());
    }
  }
  return input;
}

}  // namespace

Result<std::vector<std::uint8_t>> encode_canonical(const AccountingInput& input,
                                                   const Limits& limits) {
  ByteWriter writer;
  CCA_TRY(encode_canonical_into(writer, input, limits));
  return writer.bytes();
}

Result<AccountingInput> decode_canonical(const void* data, std::size_t size,
                                         const Limits& limits) {
  CCA_TRY(limits.validate());
  if (size > kMaxCanonicalPayloadBytes) {
    return Error::of(ErrorCode::ItemTooLarge,
                     "the canonical payload is larger than the bound")
        .with_detail("size " + to_decimal(static_cast<std::uint64_t>(size)) +
                     " exceeds " +
                     to_decimal(static_cast<std::uint64_t>(kMaxCanonicalPayloadBytes)));
  }
  ByteReader reader(data, size);
  CCA_TRY_ASSIGN(magic, reader.read_raw(kMagicSize));
  if (std::string_view(reinterpret_cast<const char*>(magic.data()), magic.size()) !=
      kMagic) {
    return Error::of(ErrorCode::MalformedRecord, "the canonical payload magic is wrong");
  }
  CCA_TRY_ASSIGN(version, reader.read_u32());
  if (version != kCanonicalFormatVersion) {
    return Error::of(ErrorCode::UnsupportedFormatVersion,
                     "the canonical payload format version is not supported")
        .with_detail("version " + to_decimal(static_cast<std::uint64_t>(version)));
  }
  CCA_TRY_ASSIGN(reserved, reader.read_u32());
  if (reserved != 0U) {
    return Error::of(ErrorCode::ReservedFieldNotZero,
                     "the canonical payload header reserved field is not zero");
  }
  CCA_TRY_ASSIGN(payload_length, reader.read_u64());
  if (payload_length != static_cast<std::uint64_t>(reader.remaining())) {
    return Error::of(ErrorCode::MalformedRecord,
                     "the declared payload length does not match the input")
        .with_detail("declared " + to_decimal(payload_length) + " actual " +
                     to_decimal(static_cast<std::uint64_t>(reader.remaining())));
  }
  return decode_payload(reader, limits);
}

Result<AccountingInput> decode_canonical(const std::vector<std::uint8_t>& bytes,
                                         const Limits& limits) {
  return decode_canonical(bytes.data(), bytes.size(), limits);
}

Result<Digest> canonical_digest(const AccountingInput& input, const Limits& limits) {
  CCA_TRY_ASSIGN(bytes, encode_canonical(input, limits));
  return Digest::of_bytes(bytes.data(), bytes.size());
}

namespace internal {

Digest contribution_content_digest(const Contribution& contribution) {
  ByteWriter writer;
  write_contribution(writer, contribution);
  return Digest::of_bytes(writer.bytes().data(), writer.bytes().size());
}

Digest evidence_content_digest(const EvidenceRecord& record) {
  ByteWriter writer;
  write_evidence(writer, record);
  return Digest::of_bytes(writer.bytes().data(), writer.bytes().size());
}

Digest group_content_digest(const ContributionGroup& group) {
  ByteWriter writer;
  write_group(writer, group);
  return Digest::of_bytes(writer.bytes().data(), writer.bytes().size());
}

Digest scope_content_digest(const AccountingScope& scope) {
  ByteWriter writer;
  write_scope(writer, scope);
  return Digest::of_bytes(writer.bytes().data(), writer.bytes().size());
}

}  // namespace internal

}  // namespace cooling_capacity_accounting
