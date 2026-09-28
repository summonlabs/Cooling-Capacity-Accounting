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

#include "cooling_capacity_accounting/evidence.hpp"

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

template <typename Enum, std::size_t N>
[[nodiscard]] Result<Enum> lookup_parse(const std::array<EnumName, N>& table,
                                        std::string_view text, const char* what) {
  for (const EnumName& entry : table) {
    if (entry.name == text) {
      return static_cast<Enum>(entry.value);
    }
  }
  return Error::of(ErrorCode::ImpossibleEnumValue, std::string("unknown ") + what)
      .with_subject(std::string(text));
}

constexpr std::array<EnumName, 14> kEvidenceKindNames{{
    {1, "Nameplate"},
    {2, "VendorDatasheet"},
    {3, "CommissioningReport"},
    {4, "CapacityTest"},
    {5, "SensorReading"},
    {6, "MaintenanceRecord"},
    {7, "OutOfServiceOrder"},
    {8, "WorkOrder"},
    {9, "OperatorDeclaration"},
    {10, "AggregatedManifest"},
    {11, "PolicyDeclaration"},
    {12, "DerivedCalculation"},
    {13, "TopologyDeclaration"},
    {99, "Unknown"},
}};

constexpr std::array<EnumName, 9> kEvidenceSourceKindNames{{
    {1, "VendorDocument"},
    {2, "CommissioningTool"},
    {3, "BmsExport"},
    {4, "DcimExport"},
    {5, "Operator"},
    {6, "TestRig"},
    {7, "SyntheticGenerator"},
    {8, "DerivedInRepository"},
    {99, "Other"},
}};

constexpr std::array<EnumName, 4> kEvidenceFreshnessNames{
    {{0, "Current"}, {1, "Stale"}, {2, "Superseded"}, {3, "Future"}}};

}  // namespace

std::string_view evidence_kind_name(EvidenceKind kind) noexcept {
  return lookup(kEvidenceKindNames, kind);
}

Result<EvidenceKind> parse_evidence_kind(std::string_view text) {
  return lookup_parse<EvidenceKind>(kEvidenceKindNames, text, "evidence kind");
}

std::string_view evidence_source_kind_name(EvidenceSourceKind kind) noexcept {
  return lookup(kEvidenceSourceKindNames, kind);
}

Result<EvidenceSourceKind> parse_evidence_source_kind(std::string_view text) {
  return lookup_parse<EvidenceSourceKind>(kEvidenceSourceKindNames, text,
                            "evidence source kind");
}

std::string_view evidence_freshness_name(EvidenceFreshness freshness) noexcept {
  return lookup(kEvidenceFreshnessNames, freshness);
}

EvidenceFreshness classify_evidence(const EvidenceRecord& record,
                                    const EvidenceBinding& current,
                                    DurationMs max_evidence_age, Timestamp now) {
  if (record.observed_at > now) {
    return EvidenceFreshness::Future;
  }
  if (record.binding.epoch != current.epoch ||
      record.binding.topology != current.topology ||
      record.binding.policy != current.policy ||
      record.binding.evidence > current.evidence) {
    return EvidenceFreshness::Superseded;
  }
  if (!max_evidence_age.is_zero()) {
    const Result<DurationMs> age = now.checked_since(record.observed_at);
    if (!age.ok() || age.value() > max_evidence_age) {
      return EvidenceFreshness::Stale;
    }
  }
  return EvidenceFreshness::Current;
}

}  // namespace cooling_capacity_accounting
