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

// Shared scaffolding for the example programs. Every example builds SYNTHETIC
// facility data: none of it was measured on real cooling equipment.

#ifndef COOLING_CAPACITY_ACCOUNTING_EXAMPLES_EXAMPLE_SUPPORT_HPP
#define COOLING_CAPACITY_ACCOUNTING_EXAMPLES_EXAMPLE_SUPPORT_HPP

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

namespace example {

using namespace cooling_capacity_accounting;

inline constexpr std::int64_t kNowMilliseconds = 1'767'225'600'000LL;  // 2026-01-01

[[nodiscard]] inline Timestamp now() {
  return Timestamp::of_unix_milliseconds(kNowMilliseconds);
}

template <typename Id>
[[nodiscard]] inline Id id_of(std::string_view text) {
  return Id::from_validated(Identifier::from_validated(std::string(text)));
}

/// A reference literal the example controls, so it needs no re-validation.
[[nodiscard]] inline DocumentRef reference_of(std::string_view text) {
  return DocumentRef::from_validated(std::string(text));
}

/// A scope with everything the accounting needs filled in.
[[nodiscard]] inline AccountingScope make_scope(std::string_view id, ScopeKind kind,
                                                std::string_view parent, Medium medium) {
  AccountingScope scope;
  scope.id = id_of<ScopeId>(id);
  scope.kind = kind;
  if (!parent.empty()) {
    scope.parent = id_of<ScopeId>(parent);
  }
  scope.medium = medium;
  scope.label = BoundedText::from_validated(std::string(id));
  scope.topology_generation = TopologyGeneration::initial();
  scope.epoch = ControlPlaneEpoch::initial();
  return scope;
}

[[nodiscard]] inline EquipmentClass make_class(std::string_view id, Medium medium,
                                               EquipmentClassKind kind) {
  EquipmentClass klass;
  klass.id = id_of<EquipmentClassId>(id);
  klass.kind = kind;
  klass.medium = medium;
  klass.label = BoundedText::from_validated(std::string(id));
  klass.contributes_to_installed = true;
  return klass;
}

/// Evidence for one contribution, observed at the example instant.
[[nodiscard]] inline EvidenceRecord make_evidence(std::string_view id,
                                                  std::int64_t declared_mw) {
  EvidenceRecord record;
  record.id = id_of<EvidenceId>(id);
  record.kind = EvidenceKind::Nameplate;
  record.source_kind = EvidenceSourceKind::SyntheticGenerator;
  record.source = id_of<EvidenceSourceId>("example.synthetic");
  record.label = BoundedText::from_validated("synthetic nameplate");
  record.reference = reference_of("synthetic://nameplate/" + std::string(id));
  record.observed_at = now();
  record.recorded_at = now();
  record.binding = EvidenceBinding{ControlPlaneEpoch::initial(),
                                   TopologyGeneration::initial(),
                                   PolicyGeneration::initial(),
                                   EvidenceGeneration::initial()};
  record.medium = Medium::Air;
  record.declared_value =
      Measure<ThermalPower>::known(ThermalPower::of_milliwatts(declared_mw));
  record.content_digest = Digest::of_text(id);
  return record;
}

[[nodiscard]] inline Contribution make_unit(std::string_view id, std::string_view scope,
                                            std::string_view klass, std::int64_t mw,
                                            std::string_view evidence) {
  Contribution contribution;
  contribution.id = id_of<ContributionId>(id);
  contribution.home_scope = id_of<ScopeId>(scope);
  contribution.equipment = id_of<EquipmentId>("equipment." + std::string(id));
  contribution.equipment_class = id_of<EquipmentClassId>(klass);
  contribution.medium = Medium::Air;
  contribution.classification = ContributionClass::Additive;
  contribution.installed = Measure<ThermalPower>::known(ThermalPower::of_milliwatts(mw));
  contribution.service = ServiceState::InService;
  contribution.binding = EvidenceBinding{ControlPlaneEpoch::initial(),
                                         TopologyGeneration::initial(),
                                         PolicyGeneration::initial(),
                                         EvidenceGeneration::initial()};
  contribution.primary_evidence = id_of<EvidenceId>(evidence);
  contribution.observed_at = now();
  return contribution;
}

/// An input with the initial generations and an evidence-backed policy.
[[nodiscard]] inline AccountingInput make_input() {
  AccountingInput input;
  input.generations = GenerationBundle::initial();
  input.policy = AccountingPolicy::baseline();
  input.policy.id = id_of<PolicyId>("cca.policy.baseline");
  input.policy.generation = input.generations.policy;
  input.policy.epoch = input.generations.epoch;
  return input;
}

/// Prints the accounting cells of a ledger in one stable line each.
inline void print_scopes(const AccountingLedger& ledger) {
  for (const ScopeAccounting& record : ledger.scopes()) {
    std::cout << "  " << record.scope.str() << " [" << scope_kind_name(record.kind)
              << "] " << closure_status_name(record.status) << " declared="
              << record.declared_installed.milliwatts() << "mW allocatable="
              << record.totals.allocatable.milliwatts() << "mW withheld="
              << record.totals.withheld.milliwatts() << "mW degraded="
              << record.totals.degraded_loss.milliwatts() << "mW unavailable="
              << record.totals.unavailable.milliwatts() << "mW indeterminate="
              << record.totals.indeterminate.milliwatts() << "mW residual="
              << record.residual.milliwatts() << "mW\n";
  }
}

inline void print_findings(const AccountingLedger& ledger) {
  for (const Finding& finding : ledger.findings()) {
    std::cout << "  finding " << finding_code_name(finding.code) << " "
              << finding_severity_name(finding.severity) << " scope="
              << (finding.scope.empty() ? std::string("-") : finding.scope.str())
              << " contribution="
              << (finding.contribution.empty() ? std::string("-")
                                               : finding.contribution.str())
              << " - " << finding.message << "\n";
  }
}

/// Checks the identity of every scope and reports it.
[[nodiscard]] inline bool check_closure(const AccountingLedger& ledger) {
  const ClosureCheck closure = ledger.verify_closure();
  if (closure.exact) {
    std::cout << "  closure: exact over " << closure.scopes_checked
              << " scopes, " << closure.scopes_conditional << " conditional\n";
    return true;
  }
  std::cout << "  closure: VIOLATED\n";
  for (const ClosureViolation& violation : closure.violations) {
    std::cout << "    " << violation.scope.str() << ": " << violation.message << "\n";
  }
  return false;
}

inline void report_error(const Error& error) {
  std::cout << "  error: " << error_code_name(error.code()) << ": " << error.to_string()
            << "\n";
}

/// A temporary directory removed when the example finishes.
class Workspace {
 public:
  explicit Workspace(std::string_view label) {
    std::error_code code;
    std::filesystem::path base = std::filesystem::temp_directory_path(code);
    if (code) {
      base = std::filesystem::current_path();
    }
    path_ = base / ("cca-example-" + std::string(label));
    std::error_code remove_code;
    static_cast<void>(std::filesystem::remove_all(path_, remove_code));
    std::error_code create_code;
    static_cast<void>(std::filesystem::create_directories(path_, create_code));
  }
  ~Workspace() {
    std::error_code code;
    static_cast<void>(std::filesystem::remove_all(path_, code));
  }
  Workspace(const Workspace&) = delete;
  Workspace& operator=(const Workspace&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace example

/// Binds a successful Result in an example, printing the error and returning
/// exit code 1 when the operation failed.
#define EXAMPLE_ASSIGN(name, expression)                                     \
  auto name##_result = (expression);                                         \
  if (!name##_result.ok()) {                                                 \
    ::example::report_error(name##_result.error());                          \
    return 1;                                                                \
  }                                                                          \
  auto& name = name##_result.value()

#define EXAMPLE_REQUIRE(condition, message)                                  \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::cout << "  FAILED: " << message << "\n";                         \
      return 1;                                                              \
    }                                                                        \
  } while (false)

#endif  // COOLING_CAPACITY_ACCOUNTING_EXAMPLES_EXAMPLE_SUPPORT_HPP
