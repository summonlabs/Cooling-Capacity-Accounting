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

// Generations, epochs and fencing: what a request is bound to, what happens
// when that binding no longer holds, and which of two generation bundles are
// comparable at all.

#include <string>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

namespace {

using namespace cooling_capacity_accounting;

/// A facility whose records are bound to the generations it is built with.
[[nodiscard]] AccountingInput bound_facility(const GenerationBundle& generations) {
  cca_test::FacilitySpec spec;
  spec.zones = 1;
  spec.loops_per_zone = 1;
  spec.units_per_loop = 2;
  spec.generations = generations;
  return cca_test::make_facility(spec);
}

}  // namespace

CCA_TEST(generations_bundle_initial_values_are_distinct_per_counter) {
  const GenerationBundle bundle = GenerationBundle::initial();
  CCA_CHECK_EQ(bundle.epoch.value(), std::uint64_t{1});
  CCA_CHECK_EQ(bundle.topology.value(), std::uint64_t{1});
  CCA_CHECK_EQ(bundle.policy.value(), std::uint64_t{1});
  CCA_CHECK_EQ(bundle.evidence.value(), std::uint64_t{1});
  CCA_CHECK_EQ(bundle.revision.value(), std::uint64_t{0});
  CCA_CHECK(bundle == GenerationBundle::initial());
  GenerationBundle advanced = bundle;
  CCA_ASSIGN(next_topology, bundle.topology.next());
  advanced.topology = next_topology;
  CCA_CHECK(advanced != bundle);
}

CCA_TEST(generations_comparison_is_total_within_an_epoch_and_incomparable_across) {
  GenerationBundle earlier = GenerationBundle::initial();
  GenerationBundle later = earlier;
  CCA_ASSIGN(later_evidence, earlier.evidence.next());
  later.evidence = later_evidence;
  CCA_CHECK_EQ(compare_generations(earlier, later), GenerationOrder::Older);
  CCA_CHECK_EQ(compare_generations(later, earlier), GenerationOrder::Newer);
  CCA_CHECK_EQ(compare_generations(earlier, earlier), GenerationOrder::Equal);

  GenerationBundle other_epoch = earlier;
  CCA_ASSIGN(epoch_two, ControlPlaneEpoch::of(2));
  other_epoch.epoch = epoch_two;
  CCA_CHECK_EQ(compare_generations(earlier, other_epoch), GenerationOrder::Incomparable);
  CCA_CHECK_EQ(compare_generations(other_epoch, earlier), GenerationOrder::Incomparable);

  // The comparison is lexicographic inside one epoch: topology outranks the
  // revision, so a later revision of an older topology is still older.
  GenerationBundle older_topology_later_revision = earlier;
  CCA_ASSIGN(revision_nine, StateRevision::of(9));
  older_topology_later_revision.revision = revision_nine;
  GenerationBundle newer_topology = earlier;
  CCA_ASSIGN(topology_two, TopologyGeneration::of(2));
  newer_topology.topology = topology_two;
  CCA_CHECK_EQ(compare_generations(older_topology_later_revision, newer_topology),
               GenerationOrder::Older);
}

CCA_TEST(generations_policy_must_match_the_accounting_generation) {
  AccountingInput input = bound_facility(GenerationBundle::initial());
  CCA_ASSIGN(second_policy_generation, PolicyGeneration::of(2));
  input.policy.generation = second_policy_generation;
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::PolicyGenerationMismatch);

  AccountingInput cross_epoch = bound_facility(GenerationBundle::initial());
  CCA_ASSIGN(second_epoch, ControlPlaneEpoch::of(2));
  cross_epoch.policy.epoch = second_epoch;
  CCA_CHECK_CODE(AccountingLedger::build(cross_epoch, cca_test::fixture_now()),
                 ErrorCode::CrossEpochAuthority);

  // A policy that is not yet effective cannot account an earlier instant.
  AccountingInput future = bound_facility(GenerationBundle::initial());
  future.policy.effective_from =
      Timestamp::of_unix_milliseconds(cca_test::fixture_now().unix_milliseconds() + 1);
  CCA_CHECK_CODE(AccountingLedger::build(future, cca_test::fixture_now()),
                 ErrorCode::FutureGeneration);
}

CCA_TEST(generations_evidence_bound_to_another_topology_is_superseded) {
  const GenerationBundle generations = GenerationBundle::initial();
  AccountingInput input = bound_facility(generations);
  // The contribution cites evidence taken against a different topology.
  CCA_ASSIGN(other_topology, TopologyGeneration::of(2));
  EvidenceRecord evidence = cca_test::make_evidence("evidence.other-topology",
                                                    EvidenceKind::Nameplate,
                                                    cca_test::fixture_ago(1'000),
                                                    generations);
  evidence.binding.topology = other_topology;
  input.evidence.push_back(evidence);
  input.contributions.front().primary_evidence = evidence.id;
  input.policy.require_installed_evidence = true;

  CCA_ASSIGN(ledger, AccountingLedger::build(input, cca_test::fixture_now()));
  bool superseded = false;
  for (const Finding& finding : ledger.findings()) {
    if (finding.code == FindingCode::SupersededEvidence) {
      superseded = true;
      CCA_CHECK_EQ(finding.severity, FindingSeverity::Error);
      CCA_CHECK(finding.evidence == evidence.id);
    }
  }
  CCA_CHECK(superseded);
  const ScopeAccounting* scope =
      ledger.find_scope(cca_test::id_of<ScopeId>(cca_test::facility_loop_id(0, 0)));
  CCA_CHECK(scope != nullptr);
  if (scope != nullptr) {
    CCA_CHECK(scope->totals.indeterminate.milliwatts() >= 250'000);
    CCA_CHECK(scope->status == ClosureStatus::Indeterminate);
  }
}

CCA_TEST(generations_observation_after_the_accounting_instant_is_refused) {
  AccountingInput input = bound_facility(GenerationBundle::initial());
  const Timestamp now = cca_test::fixture_now();
  input.contributions.front().observed_at =
      Timestamp::of_unix_milliseconds(now.unix_milliseconds() + 1);
  CCA_CHECK_CODE(AccountingLedger::build(input, now), ErrorCode::FutureTimestamp);

  AccountingInput evidence_future = bound_facility(GenerationBundle::initial());
  EvidenceRecord evidence = cca_test::make_evidence(
      "evidence.future", EvidenceKind::Nameplate,
      Timestamp::of_unix_milliseconds(now.unix_milliseconds() + 5),
      evidence_future.generations);
  evidence.recorded_at = evidence.observed_at;
  evidence_future.evidence.push_back(evidence);
  CCA_CHECK_CODE(AccountingLedger::build(evidence_future, now),
                 ErrorCode::FutureTimestamp);

  // The same instant is accepted once the accounting instant has reached it.
  CCA_ASSIGN(later_now, now.checked_add(DurationMs::of_milliseconds(10)));
  CCA_CHECK(AccountingLedger::build(evidence_future, later_now).ok());
}

CCA_TEST(generations_revision_does_not_change_the_accounting) {
  GenerationBundle first = GenerationBundle::initial();
  GenerationBundle second = first;
  CCA_ASSIGN(revision_one, StateRevision::of(1));
  second.revision = revision_one;
  CCA_ASSIGN(first_ledger,
             AccountingLedger::build(bound_facility(first), cca_test::fixture_now()));
  CCA_ASSIGN(second_ledger,
             AccountingLedger::build(bound_facility(second), cca_test::fixture_now()));
  // The revision is part of the request identity, not of the physical content,
  // so the accounting numbers are identical and only the digest differs.
  for (std::size_t index = 0; index < first_ledger.scopes().size(); ++index) {
    CCA_CHECK_EQ(first_ledger.scopes()[index].declared_installed.milliwatts(),
                 second_ledger.scopes()[index].declared_installed.milliwatts());
    CCA_CHECK_EQ(first_ledger.scopes()[index].totals.allocatable.milliwatts(),
                 second_ledger.scopes()[index].totals.allocatable.milliwatts());
  }
  CCA_CHECK(first_ledger.digest() != second_ledger.digest());
}

CCA_TEST(generations_topic_and_evidence_generations_advance_independently) {
  GenerationBundle bundle = GenerationBundle::initial();
  CCA_ASSIGN(next_topology, bundle.topology.next());
  CCA_ASSIGN(next_evidence, bundle.evidence.next());
  bundle.topology = next_topology;
  bundle.evidence = next_evidence;
  AccountingInput input = bound_facility(bundle);
  CCA_ASSIGN(ledger, AccountingLedger::build(input, cca_test::fixture_now()));
  CCA_CHECK_EQ(ledger.generations().topology.value(), std::uint64_t{2});
  CCA_CHECK_EQ(ledger.generations().evidence.value(), std::uint64_t{2});
  CCA_CHECK_EQ(ledger.generations().policy.value(), std::uint64_t{1});
  CCA_CHECK_EQ(ledger.generations().revision.value(), std::uint64_t{0});

  // A record bound to the previous topology while the accounting is at the new
  // one is superseded, so a topology move cannot silently reuse old evidence.
  AccountingInput stale = bound_facility(GenerationBundle::initial());
  stale.generations = bundle;
  stale.policy.generation = bundle.policy;
  stale.policy.epoch = bundle.epoch;
  CCA_ASSIGN(stale_ledger, AccountingLedger::build(stale, cca_test::fixture_now()));
  CCA_CHECK(stale_ledger.digest() != ledger.digest());
}
