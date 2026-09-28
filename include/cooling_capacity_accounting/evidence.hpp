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

#ifndef COOLING_CAPACITY_ACCOUNTING_EVIDENCE_HPP
#define COOLING_CAPACITY_ACCOUNTING_EVIDENCE_HPP

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "cooling_capacity_accounting/digest.hpp"
#include "cooling_capacity_accounting/domain.hpp"
#include "cooling_capacity_accounting/ids.hpp"
#include "cooling_capacity_accounting/measure.hpp"
#include "cooling_capacity_accounting/text.hpp"
#include "cooling_capacity_accounting/units.hpp"

namespace cooling_capacity_accounting {

/// What kind of fact an evidence record establishes.
enum class EvidenceKind : std::int32_t {
  Nameplate = 1,
  VendorDatasheet = 2,
  CommissioningReport = 3,
  CapacityTest = 4,
  SensorReading = 5,
  MaintenanceRecord = 6,
  OutOfServiceOrder = 7,
  WorkOrder = 8,
  OperatorDeclaration = 9,
  AggregatedManifest = 10,
  PolicyDeclaration = 11,
  DerivedCalculation = 12,
  TopologyDeclaration = 13,
  Unknown = 99,
};

[[nodiscard]] std::string_view evidence_kind_name(EvidenceKind kind) noexcept;
[[nodiscard]] Result<EvidenceKind> parse_evidence_kind(std::string_view text);

/// Where an evidence record came from. The library never contacts any of these
/// systems; the record is what arrives from them.
enum class EvidenceSourceKind : std::int32_t {
  VendorDocument = 1,
  CommissioningTool = 2,
  BmsExport = 3,
  DcimExport = 4,
  Operator = 5,
  TestRig = 6,
  SyntheticGenerator = 7,
  DerivedInRepository = 8,
  Other = 99,
};

[[nodiscard]] std::string_view evidence_source_kind_name(EvidenceSourceKind kind) noexcept;
[[nodiscard]] Result<EvidenceSourceKind> parse_evidence_source_kind(std::string_view text);

/// The generations an item of evidence was produced against. Evidence from a
/// superseded epoch, topology or policy generation cannot support an
/// authoritative claim; it is accounted as indeterminate instead.
struct EvidenceBinding {
  ControlPlaneEpoch epoch;
  TopologyGeneration topology;
  PolicyGeneration policy;
  EvidenceGeneration evidence;

  [[nodiscard]] static EvidenceBinding initial() noexcept {
    return EvidenceBinding{ControlPlaneEpoch::initial(), TopologyGeneration::initial(),
                           PolicyGeneration::initial(), EvidenceGeneration::initial()};
  }

  friend bool operator==(const EvidenceBinding& lhs,
                         const EvidenceBinding& rhs) noexcept {
    return lhs.epoch == rhs.epoch && lhs.topology == rhs.topology &&
           lhs.policy == rhs.policy && lhs.evidence == rhs.evidence;
  }
  friend bool operator!=(const EvidenceBinding& lhs,
                         const EvidenceBinding& rhs) noexcept {
    return !(lhs == rhs);
  }
};

/// One recorded fact about the facility, with its provenance.
struct EvidenceRecord {
  EvidenceId id;
  EvidenceKind kind = EvidenceKind::Unknown;
  EvidenceSourceKind source_kind = EvidenceSourceKind::Other;
  EvidenceSourceId source;
  BoundedText label;
  /// Opaque reference to the document or export this record was read from. The
  /// library never opens it.
  DocumentRef reference;
  /// When the fact was observed at the facility.
  Timestamp observed_at;
  /// When the record was entered into the accounting input.
  Timestamp recorded_at;
  /// Source-side ordering of observations from the same source.
  ObservationSequence sequence;
  EvidenceBinding binding;
  /// Digest of the record content, so a replay of the same evidence is
  /// recognisable and a changed replay is detectable.
  Digest content_digest;
  Medium medium = Medium::Air;
  std::optional<EquipmentId> subject_equipment;
  std::optional<ScopeId> subject_scope;
  /// The quantity the evidence establishes, when it establishes one. Evidence
  /// that is silent about a quantity says so instead of implying zero.
  Measure<ThermalPower> declared_value;
  BoundedText notes;
};

/// Freshness of an evidence record at an accounting instant.
enum class EvidenceFreshness : std::int32_t {
  /// Within the policy freshness window and bound to current generations.
  Current = 0,
  /// Older than the policy freshness window.
  Stale = 1,
  /// Bound to a superseded epoch, topology or policy generation.
  Superseded = 2,
  /// Observed after the accounting instant.
  Future = 3,
};

[[nodiscard]] std::string_view evidence_freshness_name(EvidenceFreshness freshness) noexcept;

/// Classifies evidence against the current generations, a freshness window and
/// an accounting instant. A zero window means the evidence never expires.
[[nodiscard]] EvidenceFreshness classify_evidence(const EvidenceRecord& record,
                                                  const EvidenceBinding& current,
                                                  DurationMs max_evidence_age,
                                                  Timestamp now);

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_EVIDENCE_HPP
