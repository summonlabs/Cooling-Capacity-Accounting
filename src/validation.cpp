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

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "cooling_capacity_accounting/clock.hpp"
#include "internal.hpp"

namespace cooling_capacity_accounting {
namespace internal {
namespace {

using ScopeIndex = std::map<std::string, std::size_t>;

[[nodiscard]] Error at(std::string_view what, const std::string& id, Error error) {
  error.with_subject(std::string(what) + " " + id);
  return error;
}

[[nodiscard]] bool medium_matches_scope(Medium medium, const AccountingScope& scope) {
  if (scope.classes.empty() && scope.kind == ScopeKind::Site) {
    return true;
  }
  if (scope.kind == ScopeKind::Site || scope.kind == ScopeKind::Zone) {
    // Sites and zones may account more than one medium; the class and the loop
    // decide the medium of a contribution.
    return true;
  }
  return scope.medium == medium;
}

[[nodiscard]] Result<void> check_scope_tree(const AccountingInput& input,
                                            const ScopeIndex& index,
                                            const Limits& limits) {
  for (const AccountingScope& scope : input.scopes) {
    if (scope.kind == ScopeKind::Site) {
      if (scope.parent.has_value()) {
        return at("scope", scope.id.str(),
                  Error::of(ErrorCode::ScopeKindMismatch, "a site may not have a parent"));
      }
    } else if (!scope.parent.has_value()) {
      return at("scope", scope.id.str(),
                Error::of(ErrorCode::OrphanScope,
                          "every scope below a site must name its parent"));
    }
    if (scope.parent.has_value()) {
      const auto parent = index.find(scope.parent->str());
      if (parent == index.end()) {
        return at("scope", scope.id.str(),
                  Error::of(ErrorCode::UnknownScope, "the parent scope does not exist")
                      .with_detail("parent " + scope.parent->str()));
      }
      const AccountingScope& parent_scope = input.scopes[parent->second];
      if (!scope_kind_may_nest_under(scope.kind, parent_scope.kind)) {
        return at("scope", scope.id.str(),
                  Error::of(ErrorCode::ScopeKindMismatch,
                            "the scope may not nest under its parent's kind")
                      .with_detail(std::string("child ") + std::string(scope_kind_name(scope.kind)) +
                                   " parent " + std::string(scope_kind_name(parent_scope.kind))));
      }
    }
    if (scope.kind == ScopeKind::ClassBand && scope.classes.size() != 1U) {
      return at("scope", scope.id.str(),
                Error::of(ErrorCode::ScopeKindMismatch,
                          "a class band declares exactly one equipment class"));
    }
    // Depth and cycles: walk up the parent chain with a visited counter.
    std::size_t depth = 0;
    const AccountingScope* cursor = &scope;
    while (cursor->parent.has_value()) {
      depth += 1;
      if (depth > limits.max_scope_nesting_depth) {
        return at("scope", scope.id.str(),
                  Error::of(ErrorCode::ScopeNestingTooDeep,
                            "the scope nests deeper than the limit allows")
                      .with_detail("depth " + to_decimal(static_cast<std::uint64_t>(depth)) +
                                   " exceeds " + to_decimal(static_cast<std::uint64_t>(
                                                     limits.max_scope_nesting_depth))));
      }
      const auto next = index.find(cursor->parent->str());
      if (next == index.end()) {
        break;
      }
      cursor = &input.scopes[next->second];
    }
    if (depth > limits.max_scope_nesting_depth) {
      return at("scope", scope.id.str(),
                Error::of(ErrorCode::ScopeNestingTooDeep, "the scope nests too deeply"));
    }
    if (cursor->parent.has_value() && cursor->parent->str() == scope.id.str()) {
      return at("scope", scope.id.str(),
                Error::of(ErrorCode::CyclicScopeNesting, "a scope is its own ancestor"));
    }
  }
  // Every scope must reach a site root.
  for (const AccountingScope& scope : input.scopes) {
    const AccountingScope* cursor = &scope;
    std::size_t hops = 0;
    while (cursor->parent.has_value()) {
      const auto next = index.find(cursor->parent->str());
      if (next == index.end()) {
        break;
      }
      cursor = &input.scopes[next->second];
      hops += 1;
      if (hops > limits.max_scope_nesting_depth + 1U) {
        return at("scope", scope.id.str(),
                  Error::of(ErrorCode::CyclicScopeNesting,
                            "the scope's ancestors form a cycle"));
      }
    }
    if (cursor->kind != ScopeKind::Site) {
      return at("scope", scope.id.str(),
                Error::of(ErrorCode::OrphanScope,
                          "the scope does not reach a site root"));
    }
    if (hops > limits.max_scope_nesting_depth) {
      return at("scope", scope.id.str(),
                Error::of(ErrorCode::ScopeNestingTooDeep,
                          "the scope nests deeper than the limit allows"));
    }
  }
  return Ok{};
}

[[nodiscard]] Result<void> check_unique_ids(const AccountingInput& input) {
  {
    std::map<std::string, bool> seen;
    for (const AccountingScope& scope : input.scopes) {
      if (!seen.emplace(scope.id.str(), true).second) {
        return at("scope", scope.id.str(),
                  Error::of(ErrorCode::DuplicateScope, "the scope identifier repeats"));
      }
    }
  }
  {
    std::map<std::string, bool> seen;
    for (const EquipmentClass& klass : input.equipment_classes) {
      if (!seen.emplace(klass.id.str(), true).second) {
        return at("equipment class", klass.id.str(),
                  Error::of(ErrorCode::DuplicateIdentity,
                            "the equipment class identifier repeats"));
      }
    }
  }
  {
    std::map<std::string, bool> seen;
    for (const IndependenceDomain& domain : input.independence_domains) {
      if (!seen.emplace(domain.id.str(), true).second) {
        return at("shared-fate domain", domain.id.str(),
                  Error::of(ErrorCode::DuplicateIndependenceDomain,
                            "the shared-fate declaration identifier repeats"));
      }
    }
  }
  {
    std::map<std::string, bool> seen;
    for (const ContributionGroup& group : input.groups) {
      if (!seen.emplace(group.id.str(), true).second) {
        return at("contribution group", group.id.str(),
                  Error::of(ErrorCode::DuplicateIdentity,
                            "the contribution group identifier repeats"));
      }
    }
  }
  {
    std::map<std::string, bool> seen;
    for (const ManifestDeclaration& declaration : input.manifest_declarations) {
      if (!seen.emplace(declaration.id.str(), true).second) {
        return at("manifest declaration", declaration.id.str(),
                  Error::of(ErrorCode::DuplicateIdentity,
                            "the manifest declaration identifier repeats"));
      }
    }
  }
  return Ok{};
}

[[nodiscard]] Result<void> check_references(const AccountingInput& input,
                                            const ScopeIndex& scopes,
                                            const std::map<std::string, std::size_t>& classes,
                                            const std::map<std::string, std::size_t>& domains,
                                            const std::map<std::string, std::size_t>& groups,
                                            const std::map<std::string, std::size_t>& evidence) {
  const auto require_scope = [&scopes](const ScopeId& id, const char* what,
                                       const std::string& owner) -> Result<void> {
    if (scopes.find(id.str()) == scopes.end()) {
      return at(what, owner,
                Error::of(ErrorCode::UnknownScope, "the named scope does not exist")
                    .with_detail("scope " + id.str()));
    }
    return Ok{};
  };
  const auto require_evidence = [&evidence](const EvidenceId& id, const char* what,
                                            const std::string& owner) -> Result<void> {
    if (id.empty()) {
      return Ok{};
    }
    if (evidence.find(id.str()) == evidence.end()) {
      return at(what, owner,
                Error::of(ErrorCode::UnknownReference,
                          "the named evidence record does not exist")
                    .with_detail("evidence " + id.str()));
    }
    return Ok{};
  };

  for (const IndependenceDomain& domain : input.independence_domains) {
    CCA_TRY(require_scope(domain.scope, "shared-fate domain", domain.id.str()));
    CCA_TRY(require_evidence(domain.evidence, "shared-fate domain", domain.id.str()));
  }
  for (const AccountingScope& scope : input.scopes) {
    for (const EquipmentClassId& klass : scope.classes) {
      if (classes.find(klass.str()) == classes.end()) {
        return at("scope", scope.id.str(),
                  Error::of(ErrorCode::ClassMismatch,
                            "the scope declares an equipment class that does not exist")
                      .with_detail("class " + klass.str()));
      }
    }
    for (const EvidenceId& reference : scope.declared_total_evidence) {
      CCA_TRY(require_evidence(reference, "scope", scope.id.str()));
    }
  }
  for (const ContributionGroup& group : input.groups) {
    CCA_TRY(require_scope(group.scope, "contribution group", group.id.str()));
    CCA_TRY(require_evidence(group.protected_quantity_evidence, "contribution group",
                             group.id.str()));
    for (const IndependenceDomainId& domain : group.independence_domains) {
      if (domains.find(domain.str()) == domains.end()) {
        return at("contribution group", group.id.str(),
                  Error::of(ErrorCode::UnknownReference,
                            "the group names a shared-fate domain that does not exist")
                      .with_detail("domain " + domain.str()));
      }
    }
  }
  for (const Contribution& contribution : input.contributions) {
    CCA_TRY(require_scope(contribution.home_scope, "contribution", contribution.id.str()));
    for (const ScopeId& alias : contribution.aliases) {
      CCA_TRY(require_scope(alias, "contribution", contribution.id.str()));
    }
    CCA_TRY(require_evidence(contribution.primary_evidence, "contribution",
                             contribution.id.str()));
    for (const EvidenceId& reference : contribution.evidence) {
      CCA_TRY(require_evidence(reference, "contribution", contribution.id.str()));
    }
    for (const DerateFactor& derate : contribution.derates) {
      CCA_TRY(require_evidence(derate.evidence, "contribution", contribution.id.str()));
    }
    if (classes.find(contribution.equipment_class.str()) == classes.end()) {
      return at("contribution", contribution.id.str(),
                Error::of(ErrorCode::ClassMismatch,
                          "the contribution names an equipment class that does not exist")
                    .with_detail("class " + contribution.equipment_class.str()));
    }
    if (contribution.group.has_value() &&
        groups.find(contribution.group->str()) == groups.end()) {
      return at("contribution", contribution.id.str(),
                Error::of(ErrorCode::ContributionGroupMissing,
                          "the contribution names a group that does not exist")
                    .with_detail("group " + contribution.group->str()));
    }
    if (contribution.independence_domain.has_value() &&
        domains.find(contribution.independence_domain->str()) == domains.end()) {
      return at("contribution", contribution.id.str(),
                Error::of(ErrorCode::UnknownReference,
                          "the contribution names a shared-fate domain that does not exist")
                    .with_detail("domain " + contribution.independence_domain->str()));
    }
    for (const ApportionmentShare& share : contribution.sharing.shares) {
      CCA_TRY(require_scope(share.target, "contribution", contribution.id.str()));
    }
  }
  for (const ManifestDeclaration& declaration : input.manifest_declarations) {
    CCA_TRY(require_scope(declaration.scope, "manifest declaration", declaration.id.str()));
    CCA_TRY(require_evidence(declaration.evidence, "manifest declaration",
                             declaration.id.str()));
    if (declaration.equipment_class.has_value() &&
        classes.find(declaration.equipment_class->str()) == classes.end()) {
      return at("manifest declaration", declaration.id.str(),
                Error::of(ErrorCode::ClassMismatch,
                          "the declaration names an equipment class that does not exist")
                    .with_detail("class " + declaration.equipment_class->str()));
    }
  }
  return Ok{};
}

}  // namespace


Result<ResolvedInput> validate_and_resolve(const AccountingInput& input,
                                           Timestamp accounted_at,
                                           const Limits& limits) {
  ResolvedInput resolved;
  CCA_TRY(limits.validate());
  CCA_TRY(input.policy.validate(limits));

  // --- structure ---------------------------------------------------------
  if (input.scopes.size() > limits.max_scopes) {
    return Error::of(ErrorCode::LimitExceeded, "too many scopes")
        .with_detail(to_decimal(static_cast<std::uint64_t>(input.scopes.size())));
  }
  if (input.contributions.size() > limits.max_contributions) {
    return Error::of(ErrorCode::LimitExceeded, "too many contributions")
        .with_detail(
            to_decimal(static_cast<std::uint64_t>(input.contributions.size())));
  }
  if (input.evidence.size() > limits.max_evidence_records) {
    return Error::of(ErrorCode::LimitExceeded, "too many evidence records")
        .with_detail(to_decimal(static_cast<std::uint64_t>(input.evidence.size())));
  }
  if (input.independence_domains.size() > limits.max_independence_domains) {
    return Error::of(ErrorCode::LimitExceeded, "too many shared-fate declarations")
        .with_detail(
            to_decimal(static_cast<std::uint64_t>(input.independence_domains.size())));
  }
  if (input.manifest_declarations.size() > limits.max_scopes) {
    return Error::of(ErrorCode::LimitExceeded, "too many manifest declarations");
  }
  if (input.equipment_classes.size() > limits.max_scopes) {
    return Error::of(ErrorCode::LimitExceeded, "too many equipment classes");
  }

  // --- policy and generation agreement -----------------------------------
  if (input.policy.generation != input.generations.policy) {
    return Error::of(ErrorCode::PolicyGenerationMismatch,
                     "the policy generation is not the generation being accounted")
        .with_detail("policy " +
                     to_decimal(input.policy.generation.value()) + " accounting " +
                     to_decimal(input.generations.policy.value()));
  }
  if (input.policy.epoch != input.generations.epoch) {
    return Error::of(ErrorCode::CrossEpochAuthority,
                     "the policy belongs to a different control-plane epoch")
        .with_detail("policy epoch " + to_decimal(input.policy.epoch.value()) +
                     " accounting epoch " +
                     to_decimal(input.generations.epoch.value()));
  }
  if (input.policy.effective_from > accounted_at) {
    return Error::of(ErrorCode::FutureGeneration,
                     "the policy is not effective at the accounting instant")
        .with_detail("effective_from " + format_utc(input.policy.effective_from));
  }

  // --- identity ----------------------------------------------------------
  CCA_TRY(check_unique_ids(input));

  AccountingInput resolved_input = input;
  sort_by(resolved_input.scopes,
          [](const AccountingScope& value) { return value.id; });
  sort_by(resolved_input.equipment_classes,
          [](const EquipmentClass& value) { return value.id; });
  sort_by(resolved_input.independence_domains,
          [](const IndependenceDomain& value) { return value.id; });
  sort_by(resolved_input.groups,
          [](const ContributionGroup& value) { return value.id; });
  sort_by(resolved_input.manifest_declarations,
          [](const ManifestDeclaration& value) { return value.id; });

  ScopeIndex scope_index;
  for (std::size_t position = 0; position < resolved_input.scopes.size(); ++position) {
    scope_index.emplace(resolved_input.scopes[position].id.str(), position);
  }
  CCA_TRY(check_scope_tree(resolved_input, scope_index, limits));

  // --- evidence identity: identical replays are deduplicated, conflicting
  //     records of the same identity are preserved rather than dropped ------
  {
    std::map<std::string, std::size_t> index;
    std::vector<EvidenceRecord> kept;
    for (const EvidenceRecord& record : resolved_input.evidence) {
      const auto found = index.find(record.id.str());
      if (found == index.end()) {
        index.emplace(record.id.str(), kept.size());
        kept.push_back(record);
        continue;
      }
      const EvidenceRecord& existing = kept[found->second];
      const Digest existing_digest = evidence_content_digest(existing);
      const Digest incoming_digest = evidence_content_digest(record);
      if (existing_digest == incoming_digest) {
        Finding finding;
        finding.code = FindingCode::AliasRecorded;
        finding.severity = FindingSeverity::Info;
        finding.message = "an identical evidence record was replayed and counted once";
        finding.evidence = record.id;
        resolved.findings.push_back(finding);
        continue;
      }
      // Deterministic resolution: the record with the smaller content digest
      // wins, so the outcome does not depend on input order.
      Finding finding;
      finding.code = FindingCode::ConflictingContributionIdentity;
      finding.severity = FindingSeverity::Error;
      finding.message =
          "two evidence records share an identifier with different content; the "
          "conflict is preserved and the digest-ordered first record is used";
      finding.evidence = record.id;
      resolved.findings.push_back(finding);
      if (incoming_digest < existing_digest) {
        kept[found->second] = record;
      }
    }
    sort_by(kept, [](const EvidenceRecord& value) { return value.id; });
    resolved_input.evidence = std::move(kept);
  }

  // --- contribution identity --------------------------------------------
  {
    std::map<std::string, std::size_t> index;
    std::vector<Contribution> kept;
    for (const Contribution& contribution : resolved_input.contributions) {
      const auto found = index.find(contribution.id.str());
      if (found == index.end()) {
        index.emplace(contribution.id.str(), kept.size());
        kept.push_back(contribution);
        continue;
      }
      const Contribution& existing = kept[found->second];
      const Digest existing_digest = contribution_content_digest(existing);
      const Digest incoming_digest = contribution_content_digest(contribution);
      if (existing_digest == incoming_digest) {
        Finding finding;
        finding.code = FindingCode::DuplicateContributionIdentity;
        finding.severity = FindingSeverity::Info;
        finding.message =
            "an identical contribution was replayed and counted exactly once";
        finding.scope = contribution.home_scope;
        finding.contribution = contribution.id;
        resolved.findings.push_back(finding);
        continue;
      }
      Finding finding;
      finding.code = FindingCode::ConflictingContributionIdentity;
      finding.severity = FindingSeverity::Error;
      finding.message =
          "two contributions share an identifier with different content; both are "
          "preserved, the digest-ordered first is counted and the other is recorded "
          "as a conflict";
      finding.scope = contribution.home_scope;
      finding.contribution = contribution.id;
      resolved.findings.push_back(finding);
      resolved.conflicting_contributions.push_back(
          incoming_digest < existing_digest ? existing : contribution);
      if (incoming_digest < existing_digest) {
        kept[found->second] = contribution;
      }
    }
    sort_by(kept, [](const Contribution& value) { return value.id; });
    resolved_input.contributions = std::move(kept);
  }

  std::map<std::string, std::size_t> class_index;
  for (std::size_t position = 0; position < resolved_input.equipment_classes.size();
       ++position) {
    class_index.emplace(resolved_input.equipment_classes[position].id.str(), position);
  }
  std::map<std::string, std::size_t> domain_index;
  for (std::size_t position = 0; position < resolved_input.independence_domains.size();
       ++position) {
    domain_index.emplace(resolved_input.independence_domains[position].id.str(), position);
  }
  std::map<std::string, std::size_t> group_index;
  for (std::size_t position = 0; position < resolved_input.groups.size(); ++position) {
    group_index.emplace(resolved_input.groups[position].id.str(), position);
  }
  std::map<std::string, std::size_t> evidence_index;
  for (std::size_t position = 0; position < resolved_input.evidence.size(); ++position) {
    evidence_index.emplace(resolved_input.evidence[position].id.str(), position);
  }

  CCA_TRY(check_references(resolved_input, scope_index, class_index, domain_index,
                           group_index, evidence_index));

  // --- group structure ---------------------------------------------------
  for (const ContributionGroup& group : resolved_input.groups) {
    if (group.classification == ContributionClass::Unspecified) {
      return at("contribution group", group.id.str(),
                Error::of(ErrorCode::ClassificationRequired,
                          "the group does not state how its members combine"));
    }
    if (!contribution_class_requires_group(group.classification)) {
      return at("contribution group", group.id.str(),
                Error::of(ErrorCode::PolicyViolation,
                          "an additive contribution belongs to no group"));
    }
    if (group.classification == ContributionClass::Substitutive &&
        (group.required_concurrent == 0 ||
         group.required_concurrent > kMaxGroupMembers)) {
      return at("contribution group", group.id.str(),
                Error::of(ErrorCode::PolicyViolation,
                          "a substitutive group must declare how many members are "
                          "required at once")
                    .with_detail("required_concurrent " +
                                 to_decimal(static_cast<std::uint64_t>(
                                     group.required_concurrent)) +
                                 " is outside 1.." +
                                 to_decimal(static_cast<std::uint64_t>(
                                     kMaxGroupMembers))));
    }
    for (std::size_t left = 0; left < group.independence_domains.size(); ++left) {
      for (std::size_t right = left + 1; right < group.independence_domains.size();
           ++right) {
        if (group.independence_domains[left] == group.independence_domains[right]) {
          return at("contribution group", group.id.str(),
                    Error::of(ErrorCode::DuplicateIdentity,
                              "the group declares the same shared-fate domain twice")
                        .with_detail("domain " +
                                     group.independence_domains[left].str()));
        }
      }
    }
  }

  // --- contribution content ---------------------------------------------
  for (const Contribution& contribution : resolved_input.contributions) {
    const AccountingScope& scope =
        resolved_input.scopes[scope_index.find(contribution.home_scope.str())->second];
    const EquipmentClass& klass =
        resolved_input.equipment_classes
            [class_index.find(contribution.equipment_class.str())->second];

    if (contribution.equipment.empty()) {
      return at("contribution", contribution.id.str(),
                Error::of(ErrorCode::MissingRequiredField,
                          "the contribution does not name its equipment"));
    }
    if (contribution.classification == ContributionClass::Unspecified &&
        resolved_input.policy.require_classification) {
      return at("contribution", contribution.id.str(),
                Error::of(ErrorCode::ClassificationRequired,
                          "the contribution does not state how it combines"));
    }
    if (contribution.service == ServiceState::Unspecified) {
      return at("contribution", contribution.id.str(),
                Error::of(ErrorCode::MissingRequiredField,
                          "the contribution does not state its service state"));
    }
    if (contribution.medium != klass.medium) {
      return at("contribution", contribution.id.str(),
                Error::of(ErrorCode::MediumMismatch,
                          "the contribution's medium is not its class's medium")
                    .with_detail(std::string("contribution ") +
                                 std::string(medium_name(contribution.medium)) +
                                 " class " + std::string(medium_name(klass.medium))));
    }
    if (scope_kind_fixes_medium(scope.kind) && scope.medium != contribution.medium) {
      return at("contribution", contribution.id.str(),
                Error::of(ErrorCode::MediumMismatch,
                          "the contribution's medium is not its scope's medium")
                    .with_detail("scope " + scope.id.str()));
    }
    if (!scope.classes.empty()) {
      bool declared = false;
      for (const EquipmentClassId& candidate : scope.classes) {
        if (candidate == contribution.equipment_class) {
          declared = true;
          break;
        }
      }
      if (!declared) {
        return at("contribution", contribution.id.str(),
                  Error::of(ErrorCode::ContributionOutsideClassBand,
                            "the scope does not declare this equipment class")
                        .with_detail("class " + contribution.equipment_class.str()));
      }
    }
    if (!medium_matches_scope(contribution.medium, scope)) {
      return at("contribution", contribution.id.str(),
                Error::of(ErrorCode::MediumMismatch,
                          "the contribution's medium is not accepted by its scope"));
    }
    for (const ScopeId& alias : contribution.aliases) {
      if (alias == contribution.home_scope) {
        return at("contribution", contribution.id.str(),
                  Error::of(ErrorCode::ShareTargetsHomeScope,
                            "the alias names the contribution's own home scope")
                        .with_detail("scope " + alias.str()));
      }
    }
    for (std::size_t left = 0; left < contribution.aliases.size(); ++left) {
      for (std::size_t right = left + 1; right < contribution.aliases.size(); ++right) {
        if (contribution.aliases[left] == contribution.aliases[right]) {
          return at("contribution", contribution.id.str(),
                    Error::of(ErrorCode::DuplicateIdentity,
                              "the same alias scope is declared twice")
                        .with_detail("scope " + contribution.aliases[left].str()));
        }
      }
    }

    // Derates.
    if (contribution.derates.size() > resolved_input.policy.max_derate_factors) {
      return at("contribution", contribution.id.str(),
                Error::of(ErrorCode::LimitExceeded,
                          "the contribution declares more derate factors than the "
                          "policy allows")
                    .with_detail(
                        "derates " +
                        to_decimal(static_cast<std::uint64_t>(
                            contribution.derates.size())) +
                        " exceeds " +
                        to_decimal(static_cast<std::uint64_t>(
                            resolved_input.policy.max_derate_factors))));
    }
    for (std::size_t left = 0; left < contribution.derates.size(); ++left) {
      const DerateFactor& derate = contribution.derates[left];
      if (derate.id.empty()) {
        return at("contribution", contribution.id.str(),
                  Error::of(ErrorCode::MissingRequiredField,
                            "a derate does not declare its identifier"));
      }
      for (std::size_t right = left + 1; right < contribution.derates.size(); ++right) {
        if (contribution.derates[left].id == contribution.derates[right].id) {
          return at("contribution", contribution.id.str(),
                    Error::of(ErrorCode::DuplicateIdentity,
                              "the contribution declares the same derate twice")
                        .with_detail("derate " + derate.id.str()));
        }
      }
      if (derate.kind == DerateKind::Factor) {
        if (!derate.absolute.is_zero()) {
          return at("contribution", contribution.id.str(),
                    Error::of(ErrorCode::DerateOutOfRange,
                              "a factor derate must not carry an absolute reduction")
                        .with_detail("derate " + derate.id.str()));
        }
      } else {
        if (!derate.factor.is_one()) {
          return at("contribution", contribution.id.str(),
                    Error::of(ErrorCode::DerateOutOfRange,
                              "an absolute derate must not carry a factor")
                        .with_detail("derate " + derate.id.str()));
        }
        if (derate.absolute.is_zero()) {
          return at("contribution", contribution.id.str(),
                    Error::of(ErrorCode::DerateOutOfRange,
                              "an absolute derate of zero reduces nothing")
                        .with_detail("derate " + derate.id.str()));
        }
      }
      if (resolved_input.policy.require_derate_evidence && derate.evidence.empty()) {
        // A missing derate evidence reference weakens trust, not the quantity,
        // so it is reported by the accounting pass as indeterminate rather than
        // refused here. It is nevertheless named deterministically.
        continue;
      }
    }
    if (contribution.service == ServiceState::Degraded &&
        contribution.derates.empty()) {
      return at("contribution", contribution.id.str(),
                Error::of(ErrorCode::PolicyViolation,
                          "a degraded contribution must declare at least one derate"));
    }
    if (contribution.service != ServiceState::Degraded &&
        !contribution.derates.empty()) {
      return at("contribution", contribution.id.str(),
                Error::of(ErrorCode::PolicyViolation,
                          "derates are only meaningful for a degraded contribution")
                    .with_detail(std::string("service ") +
                                 std::string(service_state_name(contribution.service))));
    }

    // Sharing.
    if (contribution.sharing.kind == Sharing::Kind::Exclusive) {
      if (!contribution.sharing.shares.empty()) {
        return at("contribution", contribution.id.str(),
                  Error::of(ErrorCode::MalformedRecord,
                            "an exclusive contribution declares apportionment shares"));
      }
    } else {
      if (contribution.sharing.shares.empty()) {
        return at("contribution", contribution.id.str(),
                  Error::of(ErrorCode::ApportionmentIncomplete,
                            "an apportioned contribution declares no shares"));
      }
      if (contribution.sharing.shares.size() > kMaxApportionTargets) {
        return at("contribution", contribution.id.str(),
                  Error::of(ErrorCode::LimitExceeded,
                            "the contribution declares more shares than the bound"));
      }
      std::uint64_t total_ppm = 0;
      for (std::size_t left = 0; left < contribution.sharing.shares.size(); ++left) {
        const ApportionmentShare& share = contribution.sharing.shares[left];
        if (share.target == contribution.home_scope) {
          return at("contribution", contribution.id.str(),
                    Error::of(ErrorCode::ShareTargetsHomeScope,
                              "a share targets the contribution's home scope"));
        }
        for (std::size_t right = left + 1; right < contribution.sharing.shares.size();
             ++right) {
          if (contribution.sharing.shares[left].target ==
              contribution.sharing.shares[right].target) {
            return at("contribution", contribution.id.str(),
                      Error::of(ErrorCode::DuplicateShareTarget,
                                "the same scope is targeted twice")
                          .with_detail("scope " + share.target.str()));
          }
        }
        total_ppm += share.share.ppm();
      }
      if (total_ppm > Ratio::scale()) {
        return at("contribution", contribution.id.str(),
                  Error::of(ErrorCode::OverApportioned,
                            "the shares total more than the whole contribution")
                        .with_detail("total ppm " + to_decimal(total_ppm)));
      }
      if (!contribution.installed.is_known()) {
        return at("contribution", contribution.id.str(),
                  Error::of(ErrorCode::UnknownMeasurement,
                            "an apportioned contribution must have a known installed "
                            "quantity"));
      }
    }

    // Group membership.
    if (contribution.classification != ContributionClass::Unspecified) {
      if (contribution_class_requires_group(contribution.classification)) {
        if (!contribution.group.has_value()) {
          return at("contribution", contribution.id.str(),
                    Error::of(ErrorCode::ContributionGroupMissing,
                              "the contribution class requires a group"));
        }
        const ContributionGroup& group =
            resolved_input.groups[group_index.find(contribution.group->str())->second];
        if (group.scope != contribution.home_scope) {
          return at("contribution", contribution.id.str(),
                    Error::of(ErrorCode::GroupScopeMismatch,
                              "the group's scope is not the contribution's home scope")
                          .with_detail("group " + group.id.str()));
        }
        if (group.classification != contribution.classification) {
          return at("contribution", contribution.id.str(),
                    Error::of(ErrorCode::PolicyViolation,
                              "the contribution class is not the group's class")
                          .with_detail("group " + group.id.str()));
        }
        if (contribution.sharing.kind != Sharing::Kind::Exclusive) {
          return at("contribution", contribution.id.str(),
                    Error::of(ErrorCode::GroupRequiresExclusiveSharing,
                              "a grouped contribution may not be apportioned"));
        }
      } else if (contribution.group.has_value()) {
        return at("contribution", contribution.id.str(),
                  Error::of(ErrorCode::PolicyViolation,
                            "an additive contribution may not belong to a group")
                      .with_detail("group " + contribution.group->str()));
      }
    } else if (contribution.group.has_value()) {
      // Without a classification the group's class decides; an unclassified
      // contribution in a group is only accepted when the group is additive,
      // which is refused above, so this is a structural error.
      return at("contribution", contribution.id.str(),
                Error::of(ErrorCode::ClassificationRequired,
                          "a grouped contribution must state how it combines"));
    }

    // Observed instants.
    if (contribution.observed_at > accounted_at) {
      return at("contribution", contribution.id.str(),
                Error::of(ErrorCode::FutureTimestamp,
                          "the contribution was observed after the accounting instant")
                    .with_detail("observed_at " +
                                 format_utc(contribution.observed_at) + " accounted_at " +
                                 format_utc(accounted_at)));
    }
  }

  for (const EvidenceRecord& record : resolved_input.evidence) {
    if (record.observed_at > accounted_at) {
      return at("evidence", record.id.str(),
                Error::of(ErrorCode::FutureTimestamp,
                          "the evidence was observed after the accounting instant")
                    .with_detail("observed_at " + format_utc(record.observed_at)));
    }
    if (record.recorded_at > accounted_at) {
      return at("evidence", record.id.str(),
                Error::of(ErrorCode::FutureTimestamp,
                          "the evidence was recorded after the accounting instant"));
    }
  }
  for (const ManifestDeclaration& declaration : resolved_input.manifest_declarations) {
    if (declaration.observed_at > accounted_at) {
      return at("manifest declaration", declaration.id.str(),
                Error::of(ErrorCode::FutureTimestamp,
                          "the declaration was observed after the accounting instant"));
    }
  }

  // --- quantity bounds ---------------------------------------------------
  std::int64_t total_installed = 0;
  for (const Contribution& contribution : resolved_input.contributions) {
    if (!contribution.installed.is_known()) {
      continue;
    }
    total_installed += contribution.installed.value().milliwatts();
    if (total_installed > kMaxThermalPowerMilliwatts) {
      return at("contribution", contribution.id.str(),
                Error::of(ErrorCode::OutOfRange,
                          "the total installed quantity exceeds the accounting "
                          "ceiling")
                      .with_detail(to_decimal(total_installed)));
    }
  }

  resolved.input = std::move(resolved_input);
  return resolved;
}

}  // namespace internal
}  // namespace cooling_capacity_accounting
