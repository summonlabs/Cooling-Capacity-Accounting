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

// Adversarial input. Every case here is an attempt to make the library accept
// something it should refuse, refuse something it should accept, overflow an
// exact integer, allocate from an attacker-declared size, or turn a stale or
// unknown fact into authority.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

namespace {

using namespace cooling_capacity_accounting;

[[nodiscard]] AccountingInput rich_input() {
  cca_test::FacilitySpec spec;
  spec.zones = 2;
  spec.loops_per_zone = 2;
  spec.units_per_loop = 3;
  AccountingInput input = cca_test::make_facility(spec);
  input.scopes.front().declared_installed_total =
      Measure<ThermalPower>::known(ThermalPower::of_milliwatts(1'000'000));
  AccountingScope band;
  band.id = cca_test::id_of<ScopeId>("band.0.0");
  band.kind = ScopeKind::ClassBand;
  band.parent = cca_test::id_of<ScopeId>(cca_test::facility_loop_id(0, 0));
  band.medium = Medium::Air;
  band.classes.push_back(cca_test::id_of<EquipmentClassId>(
      cca_test::facility_air_class_id()));
  band.topology_generation = TopologyGeneration::initial();
  band.epoch = ControlPlaneEpoch::initial();
  input.scopes.push_back(band);
  return input;
}

}  // namespace

CCA_TEST(adversarial_mutated_canonical_payloads_are_refused_or_canonical) {
  const AccountingInput input = rich_input();
  CCA_ASSIGN(original, encode_canonical(input));
  std::uint32_t accepted = 0;
  std::uint32_t refused = 0;
  for (std::uint64_t seed = 1; seed <= 400U; ++seed) {
    cca_test::SeededRandom random(seed * 7919U);
    std::vector<std::uint8_t> mutated = original;
    const std::uint32_t mutations = 1U + (random.next_u32() % 6U);
    for (std::uint32_t index = 0; index < mutations; ++index) {
      const std::size_t position = static_cast<std::size_t>(
          random.next_u64() % static_cast<std::uint64_t>(mutated.size()));
      mutated[position] = static_cast<std::uint8_t>(random.next_u32() & 0xFFU);
    }
    const Result<AccountingInput> decoded = decode_canonical(mutated);
    if (decoded.ok()) {
      accepted += 1;
      // Anything that is accepted must be canonical: re-encoding it must
      // reproduce the exact bytes that were accepted.
      CCA_ASSIGN(reencoded, encode_canonical(decoded.value()));
      CCA_CHECK(reencoded == mutated);
    } else {
      refused += 1;
      // The refusal must be a documented code, never an internal error.
      CCA_CHECK(decoded.error().code() != ErrorCode::InternalError);
      CCA_CHECK(!error_code_name(decoded.error().code()).empty());
    }
  }
  CCA_CHECK(accepted + refused == 400U);
  cca_test::note("mutations accepted " + std::to_string(accepted) + ", refused " +
                 std::to_string(refused));
}

CCA_TEST(adversarial_truncations_are_refused_at_every_length) {
  const AccountingInput input = rich_input();
  CCA_ASSIGN(original, encode_canonical(input));
  for (std::size_t length = 0; length < original.size(); ++length) {
    const std::vector<std::uint8_t> cut(original.begin(),
                                        original.begin() + static_cast<std::ptrdiff_t>(length));
    const Result<AccountingInput> decoded = decode_canonical(cut);
    CCA_CHECK(!decoded.ok());
    if (decoded.ok()) {
      return;
    }
    CCA_CHECK(decoded.error().code() != ErrorCode::InternalError);
  }
}

CCA_TEST(adversarial_declared_sizes_never_drive_an_allocation) {
  // A payload that declares an enormous string length must be refused by the
  // bound check, not by attempting to allocate it.
  const AccountingInput input = rich_input();
  CCA_ASSIGN(original, encode_canonical(input));
  const std::string magic("CCAIN001", 8);
  CCA_CHECK(original.size() > 32U);

  for (std::size_t position = 8U; position + 4U < original.size(); position += 4U) {
    std::vector<std::uint8_t> mutated = original;
    // Set a 32-bit field to its maximum and see whether the decoder survives.
    mutated[position] = 0xFFU;
    mutated[position + 1U] = 0xFFU;
    mutated[position + 2U] = 0xFFU;
    mutated[position + 3U] = 0xFFU;
    const Result<AccountingInput> decoded = decode_canonical(mutated);
    if (!decoded.ok()) {
      CCA_CHECK(decoded.error().code() != ErrorCode::InternalError);
    }
  }
}

CCA_TEST(adversarial_exact_integer_limits_are_respected) {
  AccountingInput single = rich_input();
  // One contribution at exactly the ceiling is accepted; the same quantity
  // twice overflows the domain total and is refused rather than wrapped.
  single.contributions.resize(1U);
  single.contributions.front().installed = Measure<ThermalPower>::known(
      ThermalPower::of_milliwatts(kMaxThermalPowerMilliwatts));
  CCA_CHECK(AccountingLedger::build(single, cca_test::fixture_now()).ok());

  AccountingInput doubled = single;
  Contribution second = single.contributions.front();
  second.id = cca_test::id_of<ContributionId>("c.second");
  second.equipment = cca_test::id_of<EquipmentId>("equipment.second");
  doubled.contributions.push_back(second);
  CCA_CHECK_CODE(AccountingLedger::build(doubled, cca_test::fixture_now()),
                 ErrorCode::OutOfRange);

  // A single quantity one past the ceiling is refused at construction.
  CCA_CHECK_CODE(ThermalPower::checked_milliwatts(kMaxThermalPowerMilliwatts + 1),
                 ErrorCode::OutOfRange);
  CCA_CHECK_CODE(ThermalPower::checked_milliwatts(-1), ErrorCode::NegativeQuantity);

  // Arithmetic that would leave the representable range is refused.
  const ThermalPower ceiling = ThermalPower::ceiling();
  CCA_CHECK_CODE(ceiling.checked_add(ThermalPower::of_milliwatts(1)),
                 ErrorCode::OutOfRange);
  CCA_CHECK_CODE(ThermalPower::zero().checked_sub(ThermalPower::of_milliwatts(1)),
                 ErrorCode::NumericUnderflow);
  CCA_CHECK_CODE(checked_mul_div(1U, 1U, 0U), ErrorCode::DivisionByZero);
}

CCA_TEST(adversarial_stale_and_future_generations_are_distinguished) {
  AccountingInput input = rich_input();
  CCA_ASSIGN(evidence_generation, EvidenceGeneration::of(5));
  input.generations.evidence = evidence_generation;
  input.policy.generation = input.generations.policy;
  // Evidence bound to an older evidence generation is superseded, which makes
  // the contribution indeterminate; it is never treated as current.
  // Evidence taken against a different topology generation cannot support a
  // claim about the current one. Evidence from an older *evidence* generation is
  // a different matter: it is simply older, and only the freshness window
  // decides whether it still counts.
  CCA_ASSIGN(other_topology, TopologyGeneration::of(2));
  EvidenceRecord superseded = cca_test::make_evidence(
      "evidence.superseded", EvidenceKind::Nameplate, cca_test::fixture_ago(1'000),
      input.generations);
  superseded.binding.topology = other_topology;
  input.evidence.push_back(superseded);
  CCA_ASSIGN(older_evidence_generation, EvidenceGeneration::of(4));
  EvidenceRecord older = cca_test::make_evidence(
      "evidence.older", EvidenceKind::Nameplate, cca_test::fixture_ago(1'000),
      input.generations);
  older.binding.evidence = older_evidence_generation;
  input.evidence.push_back(older);
  input.contributions.front().primary_evidence = superseded.id;
  CCA_ASSIGN(ledger, AccountingLedger::build(input, cca_test::fixture_now()));
  bool saw_superseded = false;
  for (const Finding& finding : ledger.findings()) {
    if (finding.code == FindingCode::SupersededEvidence) {
      saw_superseded = true;
    }
  }
  CCA_CHECK(saw_superseded);

  // The same facility with the older evidence generation instead is Current:
  // age is decided by the freshness window, not by the generation counter.
  AccountingInput aged = rich_input();
  aged.generations.evidence = evidence_generation;
  CCA_ASSIGN(older_value, EvidenceGeneration::of(4));
  EvidenceRecord still_current = cca_test::make_evidence(
      "evidence.still-current", EvidenceKind::Nameplate, cca_test::fixture_ago(1'000),
      aged.generations);
  still_current.binding.evidence = older_value;
  aged.evidence.push_back(still_current);
  aged.contributions.front().primary_evidence = still_current.id;
  CCA_ASSIGN(aged_ledger, AccountingLedger::build(aged, cca_test::fixture_now()));
  bool aged_superseded = false;
  for (const Finding& finding : aged_ledger.findings()) {
    if (finding.code == FindingCode::SupersededEvidence) {
      aged_superseded = true;
    }
  }
  CCA_CHECK(!aged_superseded);

  // An observation later than the accounting instant is refused outright.
  AccountingInput future = rich_input();
  future.contributions.front().observed_at =
      Timestamp::of_unix_milliseconds(cca_test::fixture_now().unix_milliseconds() + 1);
  CCA_CHECK_CODE(AccountingLedger::build(future, cca_test::fixture_now()),
                 ErrorCode::FutureTimestamp);

  // A policy from another epoch cannot be used to account this generation.
  AccountingInput cross = rich_input();
  CCA_ASSIGN(other_epoch, ControlPlaneEpoch::of(3));
  cross.policy.epoch = other_epoch;
  CCA_CHECK_CODE(AccountingLedger::build(cross, cca_test::fixture_now()),
                 ErrorCode::CrossEpochAuthority);
}

CCA_TEST(adversarial_deep_and_wide_structures_are_bounded) {
  AccountingInput input = rich_input();
  // A scope chain deeper than the *configured* bound is refused. The bound is
  // configurable precisely because the legal nesting rules already cap the
  // depth: a site, zone, loop and class band is the deepest legal chain.
  Limits shallow;
  shallow.max_scope_nesting_depth = 1;
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now(), shallow),
                 ErrorCode::ScopeNestingTooDeep);

  // An illegal chain is refused by the nesting rule, which is checked first.
  AccountingInput illegal = rich_input();
  ScopeId parent = cca_test::id_of<ScopeId>(cca_test::facility_loop_id(0, 0));
  for (std::size_t depth = 0; depth < kMaxScopeNestingDepth + 2U; ++depth) {
    AccountingScope band;
    band.id = cca_test::id_of<ScopeId>("band.chain." + std::to_string(depth));
    band.kind = ScopeKind::ClassBand;
    band.parent = parent;
    band.medium = Medium::Air;
    band.classes.push_back(
        cca_test::id_of<EquipmentClassId>(cca_test::facility_air_class_id()));
    band.topology_generation = TopologyGeneration::initial();
    band.epoch = ControlPlaneEpoch::initial();
    illegal.scopes.push_back(band);
    parent = band.id;
  }
  CCA_CHECK_CODE(AccountingLedger::build(illegal, cca_test::fixture_now()),
                 ErrorCode::ScopeKindMismatch);

  // More scopes than the configured limit is refused before anything else.
  AccountingInput wide = rich_input();
  Limits limits;
  limits.max_scopes = 2;
  CCA_CHECK_CODE(AccountingLedger::build(wide, cca_test::fixture_now(), limits),
                 ErrorCode::LimitExceeded);

  // A cyclic scope chain is refused.
  AccountingInput cyclic = rich_input();
  cyclic.scopes[1].parent = cyclic.scopes[2].id;
  cyclic.scopes[2].parent = cyclic.scopes[1].id;
  const Result<AccountingLedger> built =
      AccountingLedger::build(cyclic, cca_test::fixture_now());
  CCA_CHECK(!built.ok());
}
