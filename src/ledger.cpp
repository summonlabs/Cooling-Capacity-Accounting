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

#include "cooling_capacity_accounting/ledger.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "cooling_capacity_accounting/canonical.hpp"
#include "cooling_capacity_accounting/clock.hpp"
#include "internal.hpp"

namespace cooling_capacity_accounting {
namespace internal {

bool scope_kind_fixes_medium(ScopeKind kind) noexcept {
  return kind == ScopeKind::Loop || kind == ScopeKind::ClassBand;
}

MemberOrderKey member_order_key(const Contribution& contribution) {
  return MemberOrderKey{contribution.priority, contribution.id.str()};
}

bool member_order_less(const Contribution& lhs, const Contribution& rhs) {
  if (lhs.priority != rhs.priority) {
    return lhs.priority < rhs.priority;
  }
  return lhs.id < rhs.id;
}

std::string render_finding(const Finding& finding) {
  std::string text(finding_code_name(finding.code));
  text += " ";
  text += finding_severity_name(finding.severity);
  if (!finding.scope.empty()) {
    text += " scope=";
    text += finding.scope.str();
  }
  if (!finding.contribution.empty()) {
    text += " contribution=";
    text += finding.contribution.str();
  }
  if (!finding.evidence.empty()) {
    text += " evidence=";
    text += finding.evidence.str();
  }
  if (finding.amount.has_value()) {
    text += " amount_mw=";
    text += to_decimal(finding.amount->milliwatts());
  }
  if (finding.count.has_value()) {
    text += " count=";
    text += to_decimal(finding.count.value());
  }
  if (!finding.message.empty()) {
    text += " - ";
    text += finding.message;
  }
  return text;
}

Result<ThermalPower> apply_derates(ThermalPower installed,
                                   const std::vector<DerateFactor>& derates) {
  std::uint64_t absolute_total = 0;
  std::uint64_t factor_product = 1;
  std::size_t factor_count = 0;
  for (const DerateFactor& derate : derates) {
    if (derate.kind == DerateKind::Absolute) {
      const std::uint64_t value =
          static_cast<std::uint64_t>(derate.absolute.milliwatts());
      if (absolute_total > kMaxThermalPowerMilliwatts - value) {
        return Error::of(ErrorCode::NumericOverflow,
                         "the absolute derates exceed the accounting ceiling");
      }
      absolute_total += value;
      continue;
    }
    CCA_TRY_ASSIGN(product, checked_mul_add(factor_product, derate.factor.ppm(), 0U));
    factor_product = product;
    factor_count += 1;
    if (factor_count > kMaxDerateFactors) {
      return Error::of(ErrorCode::LimitExceeded,
                       "more derate factors than the hard limit allows");
    }
  }

  const std::uint64_t installed_mw = static_cast<std::uint64_t>(installed.milliwatts());
  const std::uint64_t remaining =
      absolute_total >= installed_mw ? 0U : installed_mw - absolute_total;
  if (factor_count == 0) {
    return ThermalPower::of_milliwatts(static_cast<std::int64_t>(remaining));
  }
  std::uint64_t scale = 1;
  for (std::size_t index = 0; index < factor_count; ++index) {
    scale *= Ratio::scale();
  }
  // Exact: the numerator is at most 1e15 * 1e18 and the denominator at most
  // 1e18, so the 128-bit intermediate is exact and the quotient always fits.
  const WideUInt128 numerator = wide_multiply(remaining, factor_product);
  CCA_TRY_ASSIGN(quotient, wide_div_u64(numerator, scale));
  return ThermalPower::of_milliwatts(static_cast<std::int64_t>(quotient));
}

}  // namespace internal


namespace {

using cooling_capacity_accounting::internal::apply_derates;
using cooling_capacity_accounting::internal::scope_kind_fixes_medium;
using cooling_capacity_accounting::internal::validate_and_resolve;

struct Plan {
  const Contribution* source = nullptr;
  MeasureState installed_state = MeasureState::Unknown;
  ThermalPower installed;
  ThermalPower retained;
  bool indeterminate = false;
  bool unavailable = false;
  bool unsupported = false;
  bool in_service = false;
  std::optional<WithheldReason> class_reason;
  ThermalPower withheld;
  std::vector<Finding> findings;
};

struct Allocation {
  ScopeId scope;
  EquipmentClassId equipment_class;
  ThermalPower installed;
  ThermalPower allocatable;
  ThermalPower withheld;
  ThermalPower degraded_loss;
  ThermalPower unavailable;
  ThermalPower indeterminate;
  std::optional<WithheldReason> reason;
};

struct CellKey {
  std::string scope;
  std::string equipment_class;

  friend bool operator<(const CellKey& lhs, const CellKey& rhs) {
    if (lhs.scope != rhs.scope) {
      return lhs.scope < rhs.scope;
    }
    return lhs.equipment_class < rhs.equipment_class;
  }
};

struct CellTotals {
  std::size_t contribution_count = 0;
  ThermalPower declared_installed;
  DispositionTotals totals;
  WithheldBreakdown withheld;
  bool external_total_present = false;
  ThermalPower external_total;
  ThermalDelta residual;
  std::vector<Finding> findings;
};

[[nodiscard]] ThermalPower add_power(ThermalPower lhs, ThermalPower rhs) noexcept {
  return ThermalPower::of_milliwatts(lhs.milliwatts() + rhs.milliwatts());
}

[[nodiscard]] ThermalPower take_min(ThermalPower lhs, ThermalPower rhs) noexcept {
  return lhs < rhs ? lhs : rhs;
}

[[nodiscard]] ThermalPower subtract_power(ThermalPower lhs, ThermalPower rhs) noexcept {
  return ThermalPower::of_milliwatts(lhs.milliwatts() - rhs.milliwatts());
}

[[nodiscard]] Ratio coverage_of(ThermalPower determinate, ThermalPower indeterminate) {
  const std::uint64_t determinate_mw = static_cast<std::uint64_t>(determinate.milliwatts());
  const std::uint64_t indeterminate_mw =
      static_cast<std::uint64_t>(indeterminate.milliwatts());
  const std::uint64_t total = determinate_mw + indeterminate_mw;
  if (total == 0U) {
    return Ratio::one();
  }
  const Result<std::uint64_t> ppm =
      checked_mul_div(determinate_mw, Ratio::scale(), total);
  if (!ppm.ok()) {
    return Ratio::none();
  }
  const Result<Ratio> ratio = Ratio::of_ppm(static_cast<std::uint32_t>(ppm.value()));
  return ratio.ok() ? ratio.value() : Ratio::none();
}

[[nodiscard]] FindingSeverity residual_severity(const ThermalDelta& residual,
                                                ThermalPower declared,
                                                Ratio tolerance) {
  if (residual.is_zero()) {
    return FindingSeverity::Info;
  }
  const std::uint64_t magnitude =
      static_cast<std::uint64_t>(residual.magnitude().milliwatts());
  const std::uint64_t declared_mw = static_cast<std::uint64_t>(declared.milliwatts());
  const Result<std::uint64_t> allowed = checked_mul_div(declared_mw, tolerance.ppm(),
                                                       Ratio::scale());
  if (!allowed.ok()) {
    return FindingSeverity::Error;
  }
  return magnitude <= allowed.value() ? FindingSeverity::Warning : FindingSeverity::Error;
}

[[nodiscard]] ClosureStatus status_of(bool conflicting, bool indeterminate,
                                      bool external_present, const ThermalDelta& residual,
                                      bool empty) {
  if (conflicting) {
    return ClosureStatus::Conflicting;
  }
  if (indeterminate) {
    return ClosureStatus::Indeterminate;
  }
  if (external_present && !residual.is_zero()) {
    return ClosureStatus::ClosedWithResidual;
  }
  if (empty) {
    return ClosureStatus::Empty;
  }
  return ClosureStatus::Closed;
}

[[nodiscard]] Finding make_finding(FindingCode code, FindingSeverity severity,
                                   std::string message) {
  Finding finding;
  finding.code = code;
  finding.severity = severity;
  finding.message = std::move(message);
  return finding;
}

void sort_findings(std::vector<Finding>& findings) {
  std::sort(findings.begin(), findings.end(), [](const Finding& lhs, const Finding& rhs) {
    if (lhs.scope != rhs.scope) {
      return lhs.scope < rhs.scope;
    }
    if (lhs.code != rhs.code) {
      return lhs.code < rhs.code;
    }
    if (lhs.contribution != rhs.contribution) {
      return lhs.contribution < rhs.contribution;
    }
    if (lhs.evidence != rhs.evidence) {
      return lhs.evidence < rhs.evidence;
    }
    return lhs.message < rhs.message;
  });
}

/// Places withheld mass across a contribution's allocations in canonical order.
void place_withheld(const std::vector<ThermalPower>& retained_by_allocation,
                    ThermalPower withheld_total, std::vector<ThermalPower>* out) {
  out->assign(retained_by_allocation.size(), ThermalPower::zero());
  ThermalPower remaining = withheld_total;
  for (std::size_t index = 0; index < retained_by_allocation.size(); ++index) {
    if (remaining.is_zero()) {
      break;
    }
    const ThermalPower take = take_min(retained_by_allocation[index], remaining);
    (*out)[index] = take;
    remaining = subtract_power(remaining, take);
  }
}

}  // namespace


Result<void> AccountingLedger::account(const Limits& limits) {
  const AccountingInput& input = input_;
  const AccountingPolicy& policy = input.policy;
  const EvidenceBinding current =
      EvidenceBinding{input.generations.epoch, input.generations.topology,
                      input.generations.policy, input.generations.evidence};
  static_cast<void>(limits);

  std::map<std::string, std::size_t> scope_index;
  for (std::size_t position = 0; position < input.scopes.size(); ++position) {
    scope_index.emplace(input.scopes[position].id.str(), position);
  }
  std::map<std::string, std::size_t> class_index;
  for (std::size_t position = 0; position < input.equipment_classes.size(); ++position) {
    class_index.emplace(input.equipment_classes[position].id.str(), position);
  }
  std::map<std::string, std::size_t> domain_index;
  for (std::size_t position = 0; position < input.independence_domains.size();
       ++position) {
    domain_index.emplace(input.independence_domains[position].id.str(), position);
  }
  std::map<std::string, std::size_t> evidence_index;
  for (std::size_t position = 0; position < input.evidence.size(); ++position) {
    evidence_index.emplace(input.evidence[position].id.str(), position);
  }

  // ---- pass A: per-contribution disposition -----------------------------
  std::vector<Plan> plans;
  plans.reserve(input.contributions.size());
  for (const Contribution& contribution : input.contributions) {
    Plan plan;
    plan.source = &contribution;
    plan.installed_state = contribution.installed.state();

    const EquipmentClass& klass =
        input.equipment_classes[class_index.find(contribution.equipment_class.str())
                                    ->second];
    if (!klass.contributes_to_installed) {
      plan.class_reason = WithheldReason::NonContributingClass;
      plan.findings.push_back(
          make_finding(FindingCode::NonContributingClass, FindingSeverity::Info,
                       "the equipment class is declared as not contributing to "
                       "installed cooling capacity; its mass is accounted and "
                       "withheld"));
    }

    if (plan.installed_state == MeasureState::Unsupported) {
      plan.unsupported = true;
      plan.findings.push_back(make_finding(
          FindingCode::UnsupportedQuantity, FindingSeverity::Info,
          "the installed quantity is unsupported in this configuration; the "
          "contribution accounts no mass and is not indeterminate"));
      plans.push_back(std::move(plan));
      continue;
    }
    if (plan.installed_state == MeasureState::Unknown) {
      plan.indeterminate = true;
      plan.findings.push_back(make_finding(
          FindingCode::UnknownInstalledQuantity, FindingSeverity::Warning,
          "the installed quantity is unknown, so no mass can be asserted for this "
          "contribution"));
      plans.push_back(std::move(plan));
      continue;
    }

    plan.installed = contribution.installed.value();

    // Evidence trust.
    bool evidence_ok = true;
    if (!contribution.primary_evidence.empty()) {
      const std::size_t position =
          evidence_index.find(contribution.primary_evidence.str())->second;
      const EvidenceRecord& record = input.evidence[position];
      const EvidenceFreshness freshness =
          classify_evidence(record, current, policy.max_evidence_age, accounted_at_);
      if (freshness != EvidenceFreshness::Current) {
        evidence_ok = false;
        Finding finding;
        finding.evidence = record.id;
        finding.severity = FindingSeverity::Error;
        finding.message =
            std::string("the primary evidence is ") +
            std::string(evidence_freshness_name(freshness)) +
            " at the accounting instant";
        switch (freshness) {
          case EvidenceFreshness::Stale:
            finding.code = FindingCode::StaleEvidence;
            break;
          case EvidenceFreshness::Superseded:
            finding.code = FindingCode::SupersededEvidence;
            break;
          case EvidenceFreshness::Future:
            finding.code = FindingCode::FutureEvidence;
            break;
          case EvidenceFreshness::Current:
            break;
        }
        plan.findings.push_back(finding);
      } else if (record.declared_value.is_known() &&
                 record.declared_value.value() != plan.installed) {
        Finding finding;
        finding.code = FindingCode::NameplateMismatch;
        finding.severity = FindingSeverity::Warning;
        finding.evidence = record.id;
        finding.amount = plan.installed - record.declared_value.value();
        finding.message =
            "the accounted installed quantity disagrees with the evidence it "
            "cites; the accounted quantity is kept and the difference is preserved";
        plan.findings.push_back(finding);
      }
    } else if (policy.require_installed_evidence) {
      evidence_ok = false;
      plan.findings.push_back(make_finding(
          FindingCode::MissingEvidence, FindingSeverity::Error,
          "the policy requires evidence for a known installed quantity and this "
          "contribution names none"));
    }
    if (policy.require_derate_evidence) {
      for (const DerateFactor& derate : contribution.derates) {
        if (derate.evidence.empty()) {
          evidence_ok = false;
          Finding finding = make_finding(
              FindingCode::MissingDerateEvidence, FindingSeverity::Error,
              "the policy requires evidence for every derate and this derate names "
              "none");
          plan.findings.push_back(finding);
        }
      }
    }

    // Service state.
    switch (contribution.service) {
      case ServiceState::Unknown:
      case ServiceState::Unspecified:
        plan.indeterminate = true;
        plan.findings.push_back(make_finding(
            FindingCode::UnknownServiceState, FindingSeverity::Warning,
            "the service state is not established, so the disposition of the mass "
            "cannot be established either"));
        break;
      case ServiceState::OutOfService:
        if (policy.require_out_of_service_evidence && contribution.primary_evidence.empty()) {
          plan.indeterminate = true;
          plan.findings.push_back(make_finding(
              FindingCode::MissingOutOfServiceEvidence, FindingSeverity::Error,
              "an out-of-service declaration without evidence is indeterminate "
              "rather than unavailable"));
        } else if (!evidence_ok) {
          plan.indeterminate = true;
        } else {
          plan.unavailable = true;
        }
        break;
      case ServiceState::InService:
      case ServiceState::Degraded:
        if (!evidence_ok) {
          plan.indeterminate = true;
        } else {
          plan.in_service = true;
        }
        break;
    }

    if (plan.in_service) {
      CCA_TRY_ASSIGN(retained, apply_derates(plan.installed, contribution.derates));
      plan.retained = retained;
      std::uint64_t absolute_total = 0;
      for (const DerateFactor& derate : contribution.derates) {
        if (derate.kind == DerateKind::Absolute) {
          absolute_total += static_cast<std::uint64_t>(derate.absolute.milliwatts());
        }
      }
      if (absolute_total > static_cast<std::uint64_t>(plan.installed.milliwatts())) {
        plan.findings.push_back(make_finding(
            FindingCode::AbsoluteDerateExceedsInstalled, FindingSeverity::Warning,
            "the absolute derates exceed the installed quantity; the retained "
            "quantity is zero and the excess is not silently absorbed"));
      }
      if (contribution.classification == ContributionClass::ReserveOnly) {
        plan.class_reason = WithheldReason::ReserveOnlyClass;
        plan.findings.push_back(make_finding(
            FindingCode::ReserveOnlyWithheld, FindingSeverity::Info,
            "the contribution is reserve by construction and is never allocatable"));
      }
      if (plan.class_reason.has_value()) {
        plan.withheld = plan.retained;
      }
    }
    plans.push_back(std::move(plan));
  }

  // ---- pass B: group obligations ---------------------------------------
  std::vector<ReserveObligation> obligations;
  for (const ContributionGroup& group : input.groups) {
    std::vector<Plan*> members;
    for (Plan& plan : plans) {
      if (!plan.source->group.has_value() || plan.source->group.value() != group.id) {
        continue;
      }
      if (plan.in_service) {
        members.push_back(&plan);
      }
    }
    std::sort(members.begin(), members.end(), [](const Plan* lhs, const Plan* rhs) {
      return internal::member_order_less(*lhs->source, *rhs->source);
    });

    switch (group.classification) {
      case ContributionClass::Substitutive: {
        std::size_t served = 0;
        for (Plan* member : members) {
          if (served < group.required_concurrent) {
            served += 1;
            continue;
          }
          member->withheld = member->retained;
          if (!member->class_reason.has_value()) {
            member->class_reason = WithheldReason::SubstitutiveSpare;
          }
          Finding finding = make_finding(
              FindingCode::SubstitutiveSpareWithheld, FindingSeverity::Info,
              "the substitutive group already has enough available members; this "
              "member's retained mass is withheld as spare");
          finding.scope = group.scope;
          finding.contribution = member->source->id;
          finding.amount = ThermalDelta::of_milliwatts(member->retained.milliwatts());
          member->findings.push_back(finding);
        }
        break;
      }
      case ContributionClass::MutuallyExclusive: {
        bool selected = false;
        for (Plan* member : members) {
          if (!selected) {
            selected = true;
            Finding finding = make_finding(
                FindingCode::MutualExclusionResolved, FindingSeverity::Info,
                "exactly one member of the mutually exclusive group is counted; "
                "this member was selected by declared priority and identifier order");
            finding.scope = group.scope;
            finding.contribution = member->source->id;
            member->findings.push_back(finding);
            continue;
          }
          member->withheld = member->retained;
          if (!member->class_reason.has_value()) {
            member->class_reason = WithheldReason::MutualExclusion;
          }
          Finding finding = make_finding(
              FindingCode::MutualExclusionResolved, FindingSeverity::Info,
              "the mutually exclusive group already counts another member; this "
              "member's retained mass is withheld");
          finding.scope = group.scope;
          finding.contribution = member->source->id;
          finding.amount = ThermalDelta::of_milliwatts(member->retained.milliwatts());
          member->findings.push_back(finding);
        }
        break;
      }
      case ContributionClass::Redundant: {
        ReserveObligation obligation;
        obligation.group = group.id;
        obligation.scope = group.scope;
        obligation.redundancy = group.redundancy;
        obligation.medium = group.medium;

        ThermalPower pool;
        for (const Plan* member : members) {
          pool = add_power(pool, member->retained);
        }

        const std::uint32_t copies = redundancy_copies(group.redundancy);
        const std::uint32_t failures = redundancy_failure_domains(group.redundancy);

        if (copies == 1 && failures == 0) {
          obligation.status = ObligationStatus::NoReserveRequired;
          obligation.protected_quantity =
              group.protected_quantity.is_known() ? group.protected_quantity.value()
                                                  : ThermalPower::zero();
          obligation.required_installed = obligation.protected_quantity;
          obligation.obligation = ThermalPower::zero();
          obligation.withheld = ThermalPower::zero();
          if (group.protected_quantity.is_known() &&
              obligation.required_installed > pool) {
            obligation.shortfall =
                obligation.required_installed - pool;
            Finding finding = make_finding(
                FindingCode::ReserveShortfall, FindingSeverity::Error,
                "the in-service pool is below the declared protected quantity");
            finding.scope = group.scope;
            finding.amount = obligation.shortfall;
            this->findings_.push_back(finding);
          }
          obligation.explanation =
              "redundancy class N requires no reserve beyond the protected quantity";
          obligations.push_back(std::move(obligation));
          break;
        }

        bool resolved = true;
        if (!group.protected_quantity.is_known()) {
          obligation.status = ObligationStatus::ProtectedQuantityUnknown;
          resolved = false;
        }
        std::map<std::string, ThermalPower> domain_capacity;
        if (resolved) {
          for (const Plan* member : members) {
            const Contribution& contribution = *member->source;
            if (!contribution.independence_domain.has_value()) {
              obligation.status = ObligationStatus::IndependenceNotDeclared;
              resolved = false;
              break;
            }
            const IndependenceDomainId& domain_id =
                contribution.independence_domain.value();
            bool declared_by_group = false;
            for (const IndependenceDomainId& candidate : group.independence_domains) {
              if (candidate == domain_id) {
                declared_by_group = true;
                break;
              }
            }
            if (!declared_by_group) {
              obligation.status = ObligationStatus::IndependenceNotDeclared;
              resolved = false;
              break;
            }
            const IndependenceDomain& domain =
                input.independence_domains[domain_index.find(domain_id.str())->second];
            if (domain.evidence.empty()) {
              obligation.status = ObligationStatus::MissingIndependenceEvidence;
              resolved = false;
              break;
            }
            if (evidence_index.find(domain.evidence.str()) == evidence_index.end()) {
              obligation.status = ObligationStatus::MissingIndependenceEvidence;
              resolved = false;
              break;
            }
            const EvidenceRecord& evidence =
                input.evidence[evidence_index.find(domain.evidence.str())->second];
            const EvidenceFreshness freshness = classify_evidence(
                evidence, current, policy.max_evidence_age, accounted_at_);
            if (freshness != EvidenceFreshness::Current) {
              obligation.status = ObligationStatus::MissingIndependenceEvidence;
              resolved = false;
              break;
            }
            domain_capacity[domain_id.str()] =
                add_power(domain_capacity[domain_id.str()], member->retained);
          }
        }

        if (!resolved) {
          obligation.explanation = std::string("the reserve obligation is unresolved: ") +
                                   std::string(obligation_status_name(obligation.status));
          for (Plan* member : members) {
            member->indeterminate = true;
            member->in_service = false;
            member->withheld = ThermalPower::zero();
            member->class_reason.reset();
            Finding finding = make_finding(
                FindingCode::ReserveUnresolved, FindingSeverity::Error,
                "the redundancy obligation of this group cannot be established from "
                "declared evidence, so its in-service mass is indeterminate rather "
                "than allocatable");
            finding.scope = group.scope;
            finding.contribution = member->source->id;
            member->findings.push_back(finding);
          }
          obligations.push_back(std::move(obligation));
          break;
        }

        std::vector<std::pair<std::string, ThermalPower>> ranked(domain_capacity.begin(),
                                                                 domain_capacity.end());
        std::sort(ranked.begin(), ranked.end(),
                  [](const std::pair<std::string, ThermalPower>& lhs,
                     const std::pair<std::string, ThermalPower>& rhs) {
                    if (lhs.second != rhs.second) {
                      return rhs.second < lhs.second;
                    }
                    return lhs.first < rhs.first;
                  });
        ThermalPower failure_mass;
        for (std::size_t index = 0; index < ranked.size() && index < failures; ++index) {
          failure_mass = add_power(failure_mass, ranked[index].second);
        }
        const ThermalPower protected_quantity = group.protected_quantity.value();
        ThermalPower required_installed;
        CCA_TRY_ASSIGN(copies_total, protected_quantity.checked_mul(copies));
        CCA_TRY_ASSIGN(required, copies_total.checked_add(failure_mass));
        required_installed = required;
        CCA_TRY_ASSIGN(obligation_value,
                       required_installed.checked_sub(protected_quantity));

        obligation.status = ObligationStatus::Established;
        obligation.protected_quantity = protected_quantity;
        obligation.required_installed = required_installed;
        obligation.obligation = obligation_value;
        obligation.withheld = take_min(obligation_value, pool);
        if (required_installed > pool) {
          obligation.shortfall = required_installed - pool;
          Finding finding = make_finding(
              FindingCode::ReserveShortfall, FindingSeverity::Error,
              "the in-service pool is below what the declared redundancy class "
              "requires; the shortfall is preserved and the install is not credited "
              "with capacity it does not have");
          finding.scope = group.scope;
          finding.amount = obligation.shortfall;
          this->findings_.push_back(finding);
        }
        for (const std::pair<std::string, ThermalPower>& entry : ranked) {
          CCA_TRY_ASSIGN(domain_id, IndependenceDomainId::parse(entry.first));
          obligation.ranked_domains.push_back(domain_id);
        }
        obligation.explanation =
            std::string("redundancy class ") +
            std::string(redundancy_class_name(group.redundancy)) + " with " +
            to_decimal(static_cast<std::uint64_t>(ranked.size())) +
            " declared failure domains with in-service capacity";

        // Distribute the withheld reserve across members in canonical order.
        ThermalPower remaining = obligation.withheld;
        for (Plan* member : members) {
          if (remaining.is_zero()) {
            break;
          }
          const ThermalPower take = take_min(member->retained, remaining);
          member->withheld = add_power(member->withheld, take);
          if (!member->class_reason.has_value()) {
            member->class_reason = WithheldReason::ReserveObligation;
          }
          remaining = subtract_power(remaining, take);
        }
        if (!obligation.withheld.is_zero()) {
          Finding finding = make_finding(
              FindingCode::ReserveUnresolved, FindingSeverity::Info,
              "part of the in-service pool is withheld to satisfy the declared "
              "redundancy reserve obligation");
          finding.scope = group.scope;
          finding.amount = ThermalDelta::of_milliwatts(obligation.withheld.milliwatts());
          this->findings_.push_back(finding);
        }
        obligations.push_back(std::move(obligation));
        break;
      }
      case ContributionClass::ReserveOnly:
      case ContributionClass::Additive:
      case ContributionClass::Unspecified:
        break;
    }
  }

  // ---- pass C: allocations ---------------------------------------------
  std::vector<Allocation> allocations;
  std::vector<ContributionAccounting> contribution_accounting;
  std::vector<const Contribution*> contribution_sources;
  std::vector<std::pair<std::string, Finding>> scoped_findings;

  for (const Plan& plan : plans) {
    const Contribution& contribution = *plan.source;
    ContributionAccounting record;
    record.id = contribution.id;
    record.classification = contribution.classification;
    record.service = contribution.service;
    record.installed_state = plan.installed_state;
    record.installed = plan.installed;
    record.findings = plan.findings;

    // Splitting the installed quantity across scopes. Every split is exact and
    // the remainder returns to the home scope, so the parts always re-sum.
    std::vector<std::pair<ScopeId, ThermalPower>> parts;
    if (contribution.sharing.kind == Sharing::Kind::Apportioned) {
      ThermalPower remaining = plan.installed;
      for (const ApportionmentShare& share : contribution.sharing.shares) {
        const ScaledPower scaled = plan.installed.scaled_by(share.share);
        parts.emplace_back(share.target, scaled.part);
        remaining = subtract_power(remaining, scaled.part);
      }
      if (!remaining.is_zero()) {
        Finding finding = make_finding(
            FindingCode::UnapportionedShare, FindingSeverity::Warning,
            "part of the shared contribution was not apportioned to any scope; the "
            "remainder stays in the home scope and is never distributed silently");
        finding.scope = contribution.home_scope;
        finding.contribution = contribution.id;
        finding.amount = ThermalDelta::of_milliwatts(remaining.milliwatts());
        scoped_findings.emplace_back(contribution.home_scope.str(), finding);
      }
      parts.emplace_back(contribution.home_scope, remaining);
    } else {
      parts.emplace_back(contribution.home_scope, plan.installed);
    }
    std::sort(parts.begin(), parts.end(),
              [](const std::pair<ScopeId, ThermalPower>& lhs,
                 const std::pair<ScopeId, ThermalPower>& rhs) {
                return lhs.first < rhs.first;
              });

    std::vector<ThermalPower> retained_by_part;
    retained_by_part.reserve(parts.size());
    for (const std::pair<ScopeId, ThermalPower>& part : parts) {
      if (plan.in_service) {
        CCA_TRY_ASSIGN(retained, apply_derates(part.second, contribution.derates));
        retained_by_part.push_back(retained);
      } else {
        retained_by_part.push_back(ThermalPower::zero());
      }
    }
    std::vector<ThermalPower> withheld_by_part;
    if (plan.in_service) {
      place_withheld(retained_by_part, plan.withheld, &withheld_by_part);
    } else {
      withheld_by_part.assign(parts.size(), ThermalPower::zero());
    }

    for (std::size_t index = 0; index < parts.size(); ++index) {
      Allocation allocation;
      allocation.scope = parts[index].first;
      allocation.equipment_class = contribution.equipment_class;
      allocation.installed = parts[index].second;
      if (plan.unsupported) {
        allocation.installed = ThermalPower::zero();
      } else if (plan.indeterminate) {
        allocation.indeterminate = allocation.installed;
      } else if (plan.unavailable) {
        allocation.unavailable = allocation.installed;
      } else {
        const ThermalPower retained = retained_by_part[index];
        allocation.degraded_loss = subtract_power(allocation.installed, retained);
        const ThermalPower withheld = withheld_by_part[index];
        allocation.allocatable = subtract_power(retained, withheld);
        if (!withheld.is_zero()) {
          allocation.withheld = withheld;
          allocation.reason = plan.class_reason.has_value()
                                  ? plan.class_reason
                                  : std::optional<WithheldReason>(
                                        WithheldReason::ReserveObligation);
        }
      }
      allocations.push_back(allocation);

      ContributionAllocation summary;
      summary.scope = allocation.scope;
      summary.installed = allocation.installed;
      summary.retained = plan.in_service ? retained_by_part[index]
                                         : (plan.unavailable || plan.indeterminate
                                                ? allocation.installed
                                                : ThermalPower::zero());
      summary.allocatable = allocation.allocatable;
      summary.withheld = allocation.withheld;
      summary.degraded_loss = allocation.degraded_loss;
      summary.unavailable = allocation.unavailable;
      summary.indeterminate = allocation.indeterminate;
      summary.withheld_reason = allocation.reason;
      if (!allocation.withheld.is_zero() && allocation.reason.has_value()) {
        summary.withheld_by_reason.add(allocation.reason.value(), allocation.withheld);
      }
      record.allocations.push_back(summary);
    }
    contribution_sources.push_back(&contribution);
    contribution_accounting.push_back(std::move(record));

    for (const Finding& finding : plan.findings) {
      Finding scoped = finding;
      scoped.scope = contribution.home_scope;
      scoped.contribution = contribution.id;
      scoped_findings.emplace_back(contribution.home_scope.str(), scoped);
    }
  }

  // ---- pass D: scope and class cells ------------------------------------
  std::map<CellKey, CellTotals> cells;
  std::map<std::string, CellTotals> scope_cells;
  std::map<std::string, std::size_t> scope_contribution_count;
  std::map<std::string, std::size_t> scope_unknown_installed;
  std::map<std::string, std::size_t> scope_indeterminate;
  std::map<std::string, bool> scope_fully_known;

  for (const AccountingScope& scope : input.scopes) {
    scope_fully_known[scope.id.str()] = true;
  }
  // Contributions are counted exactly once, in the scope they declare as home,
  // whatever scopes their mass was apportioned to.
  for (std::size_t index = 0; index < contribution_accounting.size(); ++index) {
    const Contribution& source = *contribution_sources[index];
    const std::string key = source.home_scope.str();
    scope_contribution_count[key] += 1;
    if (contribution_accounting[index].installed_state == MeasureState::Unknown) {
      scope_unknown_installed[key] += 1;
      scope_fully_known[key] = false;
    }
    bool any_indeterminate =
        contribution_accounting[index].installed_state == MeasureState::Unknown;
    for (const ContributionAllocation& allocation :
         contribution_accounting[index].allocations) {
      if (!allocation.indeterminate.is_zero()) {
        any_indeterminate = true;
      }
    }
    if (any_indeterminate) {
      scope_indeterminate[key] += 1;
    }
  }

  for (const Allocation& allocation : allocations) {
    const std::string scope_key = allocation.scope.str();
    const std::string class_key = allocation.equipment_class.str();
    CellKey key{scope_key, class_key};
    CellTotals& cell = cells[key];
    CellTotals& scope_cell = scope_cells[scope_key];
    cell.declared_installed = add_power(cell.declared_installed, allocation.installed);
    scope_cell.declared_installed =
        add_power(scope_cell.declared_installed, allocation.installed);
    cell.totals.allocatable = add_power(cell.totals.allocatable, allocation.allocatable);
    scope_cell.totals.allocatable =
        add_power(scope_cell.totals.allocatable, allocation.allocatable);
    cell.totals.withheld = add_power(cell.totals.withheld, allocation.withheld);
    scope_cell.totals.withheld =
        add_power(scope_cell.totals.withheld, allocation.withheld);
    cell.totals.degraded_loss =
        add_power(cell.totals.degraded_loss, allocation.degraded_loss);
    scope_cell.totals.degraded_loss =
        add_power(scope_cell.totals.degraded_loss, allocation.degraded_loss);
    cell.totals.unavailable =
        add_power(cell.totals.unavailable, allocation.unavailable);
    scope_cell.totals.unavailable =
        add_power(scope_cell.totals.unavailable, allocation.unavailable);
    cell.totals.indeterminate =
        add_power(cell.totals.indeterminate, allocation.indeterminate);
    scope_cell.totals.indeterminate =
        add_power(scope_cell.totals.indeterminate, allocation.indeterminate);
    if (!allocation.withheld.is_zero() && allocation.reason.has_value()) {
      cell.withheld.add(allocation.reason.value(), allocation.withheld);
      scope_cell.withheld.add(allocation.reason.value(), allocation.withheld);
    }
  }

  // The class cell of the home scope carries the contribution count.
  for (const Contribution* source : contribution_sources) {
    CellKey key{source->home_scope.str(), source->equipment_class.str()};
    cells[key].contribution_count += 1;
    scope_cells[source->home_scope.str()].contribution_count += 1;
  }

  // External declarations and residuals.
  for (const AccountingScope& scope : input.scopes) {
    CellTotals& scope_cell = scope_cells[scope.id.str()];
    if (scope.declared_installed_total.is_known()) {
      scope_cell.external_total_present = true;
      scope_cell.external_total =
          add_power(scope_cell.external_total, scope.declared_installed_total.value());
    } else if (scope.declared_installed_total.is_unknown() &&
               !scope.declared_total_evidence.empty()) {
      Finding finding = make_finding(
          FindingCode::DeclaredTotalMissing, FindingSeverity::Warning,
          "the scope declares an external installed total that is not established; "
          "no residual can be computed against it");
      finding.scope = scope.id;
      scoped_findings.emplace_back(scope.id.str(), finding);
    }
  }
  for (const ManifestDeclaration& declaration : input.manifest_declarations) {
    if (declaration.equipment_class.has_value()) {
      CellKey key{declaration.scope.str(), declaration.equipment_class->str()};
      CellTotals& cell = cells[key];
      cell.external_total_present = true;
      cell.external_total = add_power(cell.external_total, declaration.declared_installed);
      continue;
    }
    CellTotals& scope_cell = scope_cells[declaration.scope.str()];
    scope_cell.external_total_present = true;
    scope_cell.external_total =
        add_power(scope_cell.external_total, declaration.declared_installed);
  }

  for (auto& entry : scope_cells) {
    CellTotals& cell = entry.second;
    if (cell.external_total_present) {
      cell.residual = cell.external_total - cell.declared_installed;
      if (!cell.residual.is_zero()) {
        Finding finding = make_finding(
            FindingCode::UnexplainedResidual,
            residual_severity(cell.residual, cell.declared_installed,
                              policy.residual_tolerance),
            "the constituent accounting disagrees with the external declaration for "
            "this scope; the difference is preserved and nothing is adjusted to "
            "remove it");
        finding.scope = ScopeId::from_validated(Identifier::from_validated(entry.first));
        finding.amount = cell.residual;
        scoped_findings.emplace_back(entry.first, finding);
      }
    }
  }
  for (auto& entry : cells) {
    CellTotals& cell = entry.second;
    if (cell.external_total_present) {
      cell.residual = cell.external_total - cell.declared_installed;
    }
  }

  // Findings raised before the accounting passes - identity conflicts and
  // deduplications from validation, and the group-level reserve findings - name
  // the scope they belong to, so they are attached to that scope's cell. That
  // is what makes a conflicting scope report ClosureStatus::Conflicting instead
  // of quietly closing.
  for (const Finding& finding : this->findings_) {
    if (!finding.scope.empty()) {
      scope_cells[finding.scope.str()].findings.push_back(finding);
    }
  }

  // ---- findings and scope cells ----------------------------------------
  for (const std::pair<std::string, Finding>& entry : scoped_findings) {
    scope_cells[entry.first].findings.push_back(entry.second);
    this->findings_.push_back(entry.second);
  }

  std::vector<ScopeAccounting> scope_records;
  for (const AccountingScope& scope : input.scopes) {
    const CellTotals& cell = scope_cells[scope.id.str()];
    ScopeAccounting record;
    record.scope = scope.id;
    record.kind = scope.kind;
    record.medium = scope.medium;
    record.declared_installed = cell.declared_installed;
    record.totals = cell.totals;
    record.withheld = cell.withheld;
    record.external_total_present = cell.external_total_present;
    record.external_total = cell.external_total;
    record.residual = cell.residual;
    record.findings = cell.findings;
    record.contribution_count = scope_contribution_count[scope.id.str()];
    record.unknown_installed_count = scope_unknown_installed[scope.id.str()];
    record.indeterminate_contribution_count = scope_indeterminate[scope.id.str()];
    record.determinate_contribution_count =
        record.contribution_count >= record.indeterminate_contribution_count
            ? record.contribution_count - record.indeterminate_contribution_count
            : 0;
    record.installed_fully_known = scope_fully_known[scope.id.str()];
    const ThermalPower determinate =
        add_power(add_power(add_power(cell.totals.allocatable, cell.totals.withheld),
                            cell.totals.degraded_loss),
                  cell.totals.unavailable);
    record.coverage = coverage_of(determinate, cell.totals.indeterminate);
    bool conflicting = false;
    for (const Finding& finding : record.findings) {
      if (finding.code == FindingCode::ConflictingContributionIdentity ||
          finding.code == FindingCode::DuplicateContributionIdentity) {
        if (finding.severity == FindingSeverity::Error) {
          conflicting = true;
        }
      }
    }
    const bool empty = record.contribution_count == 0 && !record.external_total_present;
    record.status =
        status_of(conflicting, !record.installed_fully_known ||
                                   !cell.totals.indeterminate.is_zero() ||
                                   record.indeterminate_contribution_count != 0,
                  record.external_total_present, record.residual, empty);
    sort_findings(record.findings);
    DigestBuilder builder;
    builder.add_section("scope-accounting");
    builder.add_field("scope", record.scope.str());
    builder.add_field_u64("kind", static_cast<std::uint64_t>(record.kind));
    builder.add_field_u64("declared_installed_mw",
                          static_cast<std::uint64_t>(
                              record.declared_installed.milliwatts()));
    builder.add_field_u64("allocatable_mw",
                          static_cast<std::uint64_t>(record.totals.allocatable.milliwatts()));
    builder.add_field_u64("withheld_mw",
                          static_cast<std::uint64_t>(record.totals.withheld.milliwatts()));
    builder.add_field_u64("degraded_loss_mw",
                          static_cast<std::uint64_t>(record.totals.degraded_loss.milliwatts()));
    builder.add_field_u64("unavailable_mw",
                          static_cast<std::uint64_t>(record.totals.unavailable.milliwatts()));
    builder.add_field_u64("indeterminate_mw",
                          static_cast<std::uint64_t>(record.totals.indeterminate.milliwatts()));
    builder.add_field_i64("residual_mw", record.residual.milliwatts());
    builder.add_field_u64("status", static_cast<std::uint64_t>(record.status));
    record.digest = builder.finish();
    scope_records.push_back(std::move(record));
  }

  std::vector<ClassCell> cell_records;
  for (const auto& entry : cells) {
    CCA_TRY_ASSIGN(scope_id, ScopeId::parse(entry.first.scope));
    CCA_TRY_ASSIGN(class_id, EquipmentClassId::parse(entry.first.equipment_class));
    ClassCell record;
    record.scope = scope_id;
    record.equipment_class = class_id;
    const EquipmentClass& klass =
        input.equipment_classes[class_index.find(class_id.str())->second];
    record.medium = klass.medium;
    record.contribution_count = entry.second.contribution_count;
    record.declared_installed = entry.second.declared_installed;
    record.totals = entry.second.totals;
    record.withheld = entry.second.withheld;
    record.external_total_present = entry.second.external_total_present;
    record.external_total = entry.second.external_total;
    record.residual = entry.second.residual;
    record.status = status_of(false,
                              !entry.second.totals.indeterminate.is_zero(),
                              entry.second.external_total_present, entry.second.residual,
                              entry.second.contribution_count == 0 &&
                                  !entry.second.external_total_present);
    cell_records.push_back(record);
  }

  this->scopes_ = std::move(scope_records);
  this->contributions_ = std::move(contribution_accounting);
  this->obligations_ = std::move(obligations);
  this->cells_ = std::move(cell_records);
  sort_findings(this->findings_);

  DigestBuilder builder;
  builder.add_section("accounting-generation");
  CCA_TRY_ASSIGN(input_digest, canonical_digest(input, limits));
  builder.add_field("input", input_digest.to_hex());
  builder.add_field("policy", policy.digest().to_hex());
  builder.add_field_u64("epoch", input.generations.epoch.value());
  builder.add_field_u64("topology", input.generations.topology.value());
  builder.add_field_u64("policy_generation", input.generations.policy.value());
  builder.add_field_u64("evidence_generation", input.generations.evidence.value());
  builder.add_field_u64("revision", input.generations.revision.value());
  builder.add_field_i64("accounted_at", accounted_at_.unix_milliseconds());
  for (const ScopeAccounting& record : this->scopes_) {
    builder.add_field("scope_digest", record.digest.to_hex());
  }
  this->digest_ = builder.finish();
  return Ok{};
}

Result<AccountingLedger> AccountingLedger::build(AccountingInput input,
                                                 Timestamp accounted_at,
                                                 const Limits& limits) {
  CCA_TRY_ASSIGN(resolved, validate_and_resolve(input, accounted_at, limits));
  AccountingLedger ledger;
  ledger.input_ = std::move(resolved.input);
  ledger.accounted_at_ = accounted_at;
  ledger.findings_ = std::move(resolved.findings);
  ledger.conflicts_ = std::move(resolved.conflicting_contributions);
  CCA_TRY(ledger.account(limits));
  return ledger;
}

Result<void> AccountingLedger::validate_input(const AccountingInput& input,
                                              Timestamp accounted_at,
                                              const Limits& limits) {
  CCA_TRY(internal::validate_and_resolve(input, accounted_at, limits));
  return Ok{};
}

const ScopeAccounting* AccountingLedger::find_scope(const ScopeId& scope) const noexcept {
  for (const ScopeAccounting& record : scopes_) {
    if (record.scope == scope) {
      return &record;
    }
  }
  return nullptr;
}

const ContributionAccounting* AccountingLedger::find_contribution(
    const ContributionId& id) const noexcept {
  for (const ContributionAccounting& record : contributions_) {
    if (record.id == id) {
      return &record;
    }
  }
  return nullptr;
}

AccountingTotals AccountingLedger::site_totals() const {
  AccountingTotals totals;
  for (const ScopeAccounting& record : scopes_) {
    if (record.kind == ScopeKind::Site) {
      totals.add(record);
    }
  }
  totals.closed_exactly = true;
  for (const ScopeAccounting& record : scopes_) {
    if (record.status != ClosureStatus::Closed && record.status != ClosureStatus::Empty) {
      totals.closed_exactly = false;
    }
  }
  return totals;
}

AccountingTotals AccountingLedger::overall_totals() const {
  AccountingTotals totals;
  for (const ScopeAccounting& record : scopes_) {
    totals.add(record);
  }
  totals.closed_exactly = true;
  for (const ScopeAccounting& record : scopes_) {
    if (record.status != ClosureStatus::Closed && record.status != ClosureStatus::Empty) {
      totals.closed_exactly = false;
    }
  }
  return totals;
}

Result<RollupView> AccountingLedger::scope_rollup(const ScopeId& root) const {
  const ScopeAccounting* root_record = find_scope(root);
  if (root_record == nullptr) {
    return Error::of(ErrorCode::UnknownScope, "the rollup scope does not exist")
        .with_subject(root.str());
  }
  std::map<std::string, bool> included;
  included[root.str()] = true;
  bool changed = true;
  while (changed) {
    changed = false;
    for (const AccountingScope& scope : input_.scopes) {
      if (!scope.parent.has_value()) {
        continue;
      }
      if (included.find(scope.parent->str()) != included.end() &&
          included.find(scope.id.str()) == included.end()) {
        included[scope.id.str()] = true;
        changed = true;
      }
    }
  }
  RollupView view;
  view.dimension = RollupDimension::Scope;
  view.root = root.str();
  for (const AccountingScope& scope : input_.scopes) {
    if (scope.id == root) {
      view.label = scope.label;
      break;
    }
  }
  for (const AccountingScope& scope : input_.scopes) {
    if (included.find(scope.id.str()) == included.end()) {
      continue;
    }
    const ScopeAccounting* record = find_scope(scope.id);
    if (record == nullptr) {
      continue;
    }
    view.scopes.push_back(*record);
    view.totals.add(*record);
    for (const Finding& finding : record->findings) {
      view.findings.push_back(finding);
    }
  }
  view.totals.closed_exactly = true;
  for (const ScopeAccounting& record : view.scopes) {
    if (record.status != ClosureStatus::Closed && record.status != ClosureStatus::Empty) {
      view.totals.closed_exactly = false;
    }
  }
  sort_findings(view.findings);
  DigestBuilder builder;
  builder.add_section("scope-rollup");
  builder.add_field("root", view.root);
  for (const ScopeAccounting& record : view.scopes) {
    builder.add_field("scope_digest", record.digest.to_hex());
  }
  view.digest = builder.finish();
  return view;
}

Result<RollupView> AccountingLedger::class_rollup_from_cells(
    const EquipmentClassId& klass, Medium medium_filter, bool filter_by_medium,
    RollupDimension dimension, std::string root) const {
  bool found = false;
  RollupView view;
  view.dimension = dimension;
  view.root = std::move(root);
  std::map<std::string, ScopeAccounting> per_scope;
  for (const ClassCell& cell : cells_) {
    if (dimension == RollupDimension::EquipmentClass && cell.equipment_class != klass) {
      continue;
    }
    if (filter_by_medium && cell.medium != medium_filter) {
      continue;
    }
    found = true;
    ScopeAccounting& record = per_scope[cell.scope.str()];
    if (record.scope.empty()) {
      record.scope = cell.scope;
      const ScopeAccounting* scope_record = find_scope(cell.scope);
      if (scope_record != nullptr) {
        record.kind = scope_record->kind;
      }
      record.medium = cell.medium;
      record.status = cell.status;
    }
    record.declared_installed = add_power(record.declared_installed, cell.declared_installed);
    record.totals.allocatable = add_power(record.totals.allocatable, cell.totals.allocatable);
    record.totals.withheld = add_power(record.totals.withheld, cell.totals.withheld);
    record.totals.degraded_loss =
        add_power(record.totals.degraded_loss, cell.totals.degraded_loss);
    record.totals.unavailable =
        add_power(record.totals.unavailable, cell.totals.unavailable);
    record.totals.indeterminate =
        add_power(record.totals.indeterminate, cell.totals.indeterminate);
    for (std::size_t index = 0; index < kWithheldReasonCount; ++index) {
      const auto reason = static_cast<WithheldReason>(index);
      record.withheld.add(reason, cell.withheld.of(reason));
    }
    record.contribution_count += cell.contribution_count;
    record.external_total_present =
        record.external_total_present || cell.external_total_present;
    record.external_total = add_power(record.external_total, cell.external_total);
    record.residual =
        ThermalDelta::of_milliwatts(record.residual.milliwatts() +
                                    cell.residual.milliwatts());
  }
  if (!found) {
    return Error::of(ErrorCode::UnknownReference,
                     "no accounted cell matches the requested rollup")
        .with_subject(view.root);
  }
  for (auto& entry : per_scope) {
    ScopeAccounting& record = entry.second;
    record.installed_fully_known = record.unknown_installed_count == 0;
    record.coverage = coverage_of(
        add_power(add_power(add_power(record.totals.allocatable, record.totals.withheld),
                            record.totals.degraded_loss),
                  record.totals.unavailable),
        record.totals.indeterminate);
    record.status = status_of(false, !record.totals.indeterminate.is_zero(),
                              record.external_total_present, record.residual,
                              record.contribution_count == 0 &&
                                  !record.external_total_present);
    const ScopeAccounting* full = find_scope(record.scope);
    if (full != nullptr) {
      for (const Finding& finding : full->findings) {
        if (finding.scope == record.scope) {
          record.findings.push_back(finding);
        }
      }
    }
    view.totals.add(record);
    view.scopes.push_back(record);
  }
  view.totals.closed_exactly = true;
  for (const ScopeAccounting& record : view.scopes) {
    if (record.status != ClosureStatus::Closed && record.status != ClosureStatus::Empty) {
      view.totals.closed_exactly = false;
    }
  }
  DigestBuilder builder;
  builder.add_section("cell-rollup");
  builder.add_field("root", view.root);
  for (const ScopeAccounting& record : view.scopes) {
    builder.add_field("scope_digest", record.digest.to_hex());
    builder.add_field_u64("declared_installed_mw",
                          static_cast<std::uint64_t>(
                              record.declared_installed.milliwatts()));
  }
  view.digest = builder.finish();
  return view;
}

Result<RollupView> AccountingLedger::class_rollup(const EquipmentClassId& klass) const {
  return class_rollup_from_cells(klass, Medium::Air, false,
                                 RollupDimension::EquipmentClass, klass.str());
}

Result<RollupView> AccountingLedger::medium_rollup(Medium medium) const {
  std::string root(medium_name(medium));
  return class_rollup_from_cells(EquipmentClassId(), medium, true,
                                 RollupDimension::Medium, std::move(root));
}

ClosureCheck AccountingLedger::verify_closure() const {
  ClosureCheck check;
  for (const ScopeAccounting& record : scopes_) {
    check.scopes_checked += 1;
    if (!record.installed_fully_known) {
      check.scopes_conditional += 1;
    }
    const Result<void> identity = record.totals.verify(record.declared_installed);
    if (!identity.ok()) {
      ClosureViolation violation;
      violation.scope = record.scope;
      violation.declared_installed = record.declared_installed;
      violation.decomposed = record.totals.sum();
      violation.difference = record.declared_installed - record.totals.sum();
      violation.message = identity.error().to_string();
      check.violations.push_back(violation);
      check.exact = false;
    }
    const Result<void> withheld = record.withheld.verify_total();
    if (!withheld.ok()) {
      ClosureViolation violation;
      violation.scope = record.scope;
      violation.message = withheld.error().to_string();
      check.violations.push_back(violation);
      check.exact = false;
    }
  }
  for (const ClassCell& cell : cells_) {
    const Result<void> identity = cell.totals.verify(cell.declared_installed);
    if (!identity.ok()) {
      ClosureViolation violation;
      violation.scope = cell.scope;
      violation.declared_installed = cell.declared_installed;
      violation.decomposed = cell.totals.sum();
      violation.difference = cell.declared_installed - cell.totals.sum();
      violation.message = std::string("equipment class ") +
                          cell.equipment_class.str() + ": " +
                          identity.error().to_string();
      check.violations.push_back(violation);
      check.exact = false;
    }
  }
  return check;
}

std::string AccountingLedger::to_report() const {
  std::string text;
  text += "accounting generation at ";
  text += format_utc(accounted_at_);
  text += "\n";
  text += "  generations: epoch=";
  text += to_decimal(input_.generations.epoch.value());
  text += " topology=";
  text += to_decimal(input_.generations.topology.value());
  text += " policy=";
  text += to_decimal(input_.generations.policy.value());
  text += " evidence=";
  text += to_decimal(input_.generations.evidence.value());
  text += " revision=";
  text += to_decimal(input_.generations.revision.value());
  text += "\n  ";
  text += input_.policy.to_string();
  text += "\n  digest=";
  text += digest_.to_hex();
  text += "\n";
  for (const ScopeAccounting& record : scopes_) {
    text += "  scope ";
    text += record.scope.str();
    text += " [";
    text += scope_kind_name(record.kind);
    text += "] status=";
    text += closure_status_name(record.status);
    text += " declared=";
    text += to_decimal(record.declared_installed.milliwatts());
    text += "mW allocatable=";
    text += to_decimal(record.totals.allocatable.milliwatts());
    text += "mW withheld=";
    text += to_decimal(record.totals.withheld.milliwatts());
    text += "mW degraded_loss=";
    text += to_decimal(record.totals.degraded_loss.milliwatts());
    text += "mW unavailable=";
    text += to_decimal(record.totals.unavailable.milliwatts());
    text += "mW indeterminate=";
    text += to_decimal(record.totals.indeterminate.milliwatts());
    text += "mW residual=";
    text += to_decimal(record.residual.milliwatts());
    text += "mW coverage_ppm=";
    text += to_decimal(static_cast<std::uint64_t>(record.coverage.ppm()));
    text += " contributions=";
    text += to_decimal(static_cast<std::uint64_t>(record.contribution_count));
    if (record.unknown_installed_count != 0) {
      text += " unknown_quantities=";
      text += to_decimal(static_cast<std::uint64_t>(record.unknown_installed_count));
    }
    text += record.installed_fully_known ? "" : " identity=conditional";
    text += "\n";
  }
  for (const ReserveObligation& obligation : obligations_) {
    text += "  reserve group ";
    text += obligation.group.str();
    text += " class=";
    text += redundancy_class_name(obligation.redundancy);
    text += " status=";
    text += obligation_status_name(obligation.status);
    text += " protected=";
    text += to_decimal(obligation.protected_quantity.milliwatts());
    text += "mW required=";
    text += to_decimal(obligation.required_installed.milliwatts());
    text += "mW obligation=";
    text += to_decimal(obligation.obligation.milliwatts());
    text += "mW withheld=";
    text += to_decimal(obligation.withheld.milliwatts());
    text += "mW shortfall=";
    text += to_decimal(obligation.shortfall.milliwatts());
    text += "mW\n";
  }
  for (const Finding& finding : findings_) {
    text += "  finding ";
    text += internal::render_finding(finding);
    text += "\n";
  }
  for (const Contribution& conflict : conflicts_) {
    text += "  conflict preserved contribution ";
    text += conflict.id.str();
    text += " digest=";
    text += internal::contribution_content_digest(conflict).to_hex();
    text += "\n";
  }
  return text;
}

}  // namespace cooling_capacity_accounting
