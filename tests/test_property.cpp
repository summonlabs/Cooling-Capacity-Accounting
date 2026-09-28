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
#include <cstdint>
#include <string>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "reference_model.hpp"
#include "test_harness.hpp"

namespace {

using namespace cooling_capacity_accounting;
using cca_test::SeededRandom;

/// Builds one randomised synthetic facility in the subset the independent
/// reference model covers: additive contributions, exclusive sharing, known
/// installed quantities. Everything else is exercised elsewhere.
[[nodiscard]] AccountingInput random_additive_facility(SeededRandom& random,
                                                       std::size_t contributions) {
  AccountingInput input;
  input.generations = GenerationBundle::initial();
  input.policy = cca_test::relaxed_policy();
  input.policy.generation = input.generations.policy;
  input.policy.epoch = input.generations.epoch;

  input.equipment_classes.push_back(cca_test::make_class_for_property_tests());

  input.scopes.push_back(cca_test::make_scope_for_property_tests(
      "site.p", ScopeKind::Site, "", Medium::Air));
  for (std::size_t zone = 0; zone < 3U; ++zone) {
    const std::string zone_id = "zone.p" + std::to_string(zone);
    input.scopes.push_back(cca_test::make_scope_for_property_tests(
        zone_id, ScopeKind::Zone, "site.p", Medium::Air));
    const std::string loop_id = "loop.p" + std::to_string(zone);
    input.scopes.push_back(cca_test::make_scope_for_property_tests(
        loop_id, ScopeKind::Loop, zone_id, Medium::Air));
  }

  for (std::size_t index = 0; index < contributions; ++index) {
    const std::size_t zone = index % 3U;
    Contribution contribution;
    contribution.id = cca_test::id_of<ContributionId>("c." + std::to_string(index));
    contribution.home_scope =
        cca_test::id_of<ScopeId>("loop.p" + std::to_string(zone));
    contribution.equipment =
        cca_test::id_of<EquipmentId>("equipment." + std::to_string(index));
    contribution.equipment_class =
        cca_test::id_of<EquipmentClassId>(cca_test::property_class_id());
    contribution.medium = Medium::Air;
    contribution.classification = ContributionClass::Additive;
    contribution.installed = Measure<ThermalPower>::known(
        ThermalPower::of_milliwatts(random.next_range(0, 1'000'000)));
    const std::uint32_t draw = random.next_u32() % 100U;
    if (draw < 70U) {
      contribution.service = ServiceState::InService;
    } else if (draw < 90U) {
      contribution.service = ServiceState::Degraded;
      const std::uint32_t factors = 1U + (random.next_u32() % 3U);
      for (std::uint32_t factor = 0; factor < factors; ++factor) {
        DerateFactor derate;
        derate.id = cca_test::id_of<DerateId>("derate." + std::to_string(index) + "." +
                                             std::to_string(factor));
        derate.kind = DerateKind::Factor;
        derate.factor = Ratio::of_ppm(500'000U + (random.next_u32() % 500'001U)).value();
        contribution.derates.push_back(derate);
      }
    } else if (draw < 95U) {
      contribution.service = ServiceState::OutOfService;
    } else {
      contribution.service = ServiceState::Unknown;
    }
    contribution.binding =
        EvidenceBinding{input.generations.epoch, input.generations.topology,
                        input.generations.policy, input.generations.evidence};
    contribution.observed_at = cca_test::fixture_ago(1'000);
    input.contributions.push_back(contribution);
  }
  return input;
}

/// Shuffles every vector of an input and every nested list of every record.
void shuffle_input(AccountingInput& input, SeededRandom& random) {
  const auto shuffle = [&random](auto& values) {
    for (std::size_t index = values.size(); index > 1U; --index) {
      const std::size_t other = static_cast<std::size_t>(
          random.next_u64() % static_cast<std::uint64_t>(index));
      std::swap(values[index - 1U], values[other]);
    }
  };
  shuffle(input.scopes);
  shuffle(input.contributions);
  shuffle(input.evidence);
  shuffle(input.groups);
  shuffle(input.independence_domains);
  shuffle(input.manifest_declarations);
  shuffle(input.equipment_classes);
  for (Contribution& contribution : input.contributions) {
    shuffle(contribution.derates);
    shuffle(contribution.sharing.shares);
    shuffle(contribution.aliases);
    shuffle(contribution.evidence);
  }
}

}  // namespace

CCA_TEST(property_identity_holds_for_random_facilities) {
  for (std::uint64_t seed = 1; seed <= 25U; ++seed) {
    SeededRandom random(seed);
    const std::size_t contributions = 1U + (random.next_u32() % 60U);
    const AccountingInput input = random_additive_facility(random, contributions);
    CCA_ASSIGN(ledger, AccountingLedger::build(input, cca_test::fixture_now()));
    const ClosureCheck closure = ledger.verify_closure();
    if (!closure.exact) {
      cca_test::note("seed " + std::to_string(seed) + " closure violations " +
                     std::to_string(closure.violations.size()));
      for (const ClosureViolation& violation : closure.violations) {
        cca_test::note("  " + violation.scope.str() + ": " + violation.message);
      }
    }
    CCA_CHECK(closure.exact);
    for (const ScopeAccounting& record : ledger.scopes()) {
      CCA_CHECK(record.totals.verify(record.declared_installed).ok());
      CCA_CHECK(record.withheld.verify_total().ok());
    }
  }
}

CCA_TEST(property_matches_the_independent_reference_model) {
  for (std::uint64_t seed = 101; seed <= 125U; ++seed) {
    SeededRandom random(seed);
    const std::size_t contributions = 1U + (random.next_u32() % 40U);
    const AccountingInput input = random_additive_facility(random, contributions);
    CCA_ASSIGN(ledger, AccountingLedger::build(input, cca_test::fixture_now()));
    CCA_ASSIGN(reference, cca_test::ReferenceModel::build(input));

    for (const ScopeAccounting& record : ledger.scopes()) {
      const cca_test::ReferenceTotals& expected = reference.scope(record.scope.str());
      if (record.declared_installed.milliwatts() != expected.declared_installed ||
          record.totals.allocatable.milliwatts() != expected.allocatable ||
          record.totals.degraded_loss.milliwatts() != expected.degraded_loss ||
          record.totals.unavailable.milliwatts() != expected.unavailable ||
          record.totals.indeterminate.milliwatts() != expected.indeterminate) {
        cca_test::note("seed " + std::to_string(seed) + " scope " +
                       record.scope.str() + " ledger declared=" +
                       std::to_string(record.declared_installed.milliwatts()) +
                       " allocatable=" +
                       std::to_string(record.totals.allocatable.milliwatts()) +
                       " degraded=" +
                       std::to_string(record.totals.degraded_loss.milliwatts()) +
                       " unavailable=" +
                       std::to_string(record.totals.unavailable.milliwatts()) +
                       " indeterminate=" +
                       std::to_string(record.totals.indeterminate.milliwatts()) +
                       " reference declared=" + std::to_string(expected.declared_installed) +
                       " allocatable=" + std::to_string(expected.allocatable) +
                       " degraded=" + std::to_string(expected.degraded_loss) +
                       " unavailable=" + std::to_string(expected.unavailable) +
                       " indeterminate=" + std::to_string(expected.indeterminate));
      }
      CCA_CHECK_EQ(record.declared_installed.milliwatts(), expected.declared_installed);
      CCA_CHECK_EQ(record.totals.allocatable.milliwatts(), expected.allocatable);
      CCA_CHECK_EQ(record.totals.degraded_loss.milliwatts(), expected.degraded_loss);
      CCA_CHECK_EQ(record.totals.unavailable.milliwatts(), expected.unavailable);
      CCA_CHECK_EQ(record.totals.indeterminate.milliwatts(), expected.indeterminate);
    }

    // The overall totals reconstruct the reference totals exactly.
    const AccountingTotals totals = ledger.overall_totals();
    const cca_test::ReferenceTotals expected = reference.overall();
    CCA_CHECK_EQ(totals.declared_installed.milliwatts(), expected.declared_installed);
    CCA_CHECK_EQ(totals.totals.allocatable.milliwatts(), expected.allocatable);
    CCA_CHECK_EQ(totals.totals.degraded_loss.milliwatts(), expected.degraded_loss);
    CCA_CHECK_EQ(totals.totals.unavailable.milliwatts(), expected.unavailable);
    CCA_CHECK_EQ(totals.totals.indeterminate.milliwatts(), expected.indeterminate);
  }
}

CCA_TEST(property_deterministic_aggregation_is_independent_of_input_order) {
  for (std::uint64_t seed = 201; seed <= 215U; ++seed) {
    SeededRandom random(seed);
    const std::size_t contributions = 1U + (random.next_u32() % 40U);
    AccountingInput input = random_additive_facility(random, contributions);
    CCA_ASSIGN(ledger, AccountingLedger::build(input, cca_test::fixture_now()));
    CCA_ASSIGN(bytes, encode_canonical(input));

    for (int attempt = 0; attempt < 5; ++attempt) {
      AccountingInput shuffled = input;
      shuffle_input(shuffled, random);
      CCA_ASSIGN(shuffled_ledger,
                 AccountingLedger::build(shuffled, cca_test::fixture_now()));
      CCA_ASSIGN(shuffled_bytes, encode_canonical(shuffled));
      CCA_CHECK(shuffled_ledger.digest() == ledger.digest());
      CCA_CHECK(shuffled_bytes == bytes);
      CCA_CHECK(ledger.scopes().size() == shuffled_ledger.scopes().size());
      for (std::size_t index = 0; index < ledger.scopes().size(); ++index) {
        CCA_CHECK(ledger.scopes()[index].scope == shuffled_ledger.scopes()[index].scope);
        CCA_CHECK(ledger.scopes()[index].digest == shuffled_ledger.scopes()[index].digest);
      }
    }
  }
}

CCA_TEST(property_mass_is_conserved_across_the_whole_generation) {
  for (std::uint64_t seed = 301; seed <= 310U; ++seed) {
    SeededRandom random(seed);
    const std::size_t contributions = 1U + (random.next_u32() % 50U);
    const AccountingInput input = random_additive_facility(random, contributions);
    CCA_ASSIGN(ledger, AccountingLedger::build(input, cca_test::fixture_now()));

    std::int64_t accounted = 0;
    for (const ContributionAccounting& record : ledger.contributions()) {
      accounted += record.installed.milliwatts();
    }
    std::int64_t decomposed = 0;
    for (const ScopeAccounting& record : ledger.scopes()) {
      decomposed += record.declared_installed.milliwatts();
    }
    CCA_CHECK_EQ(accounted, decomposed);
    CCA_CHECK_EQ(ledger.overall_totals().declared_installed.milliwatts(), decomposed);
  }
}

CCA_TEST(property_derate_composition_matches_exact_rational_arithmetic) {
  for (std::uint64_t seed = 401; seed <= 425U; ++seed) {
    SeededRandom random(seed);
    const std::int64_t installed = random.next_range(0, 1'000'000'000);
    const std::uint32_t first = 1U + (random.next_u32() % 1'000'000U);
    const std::uint32_t second = 1U + (random.next_u32() % 1'000'000U);
    const std::uint32_t third = 1U + (random.next_u32() % 1'000'000U);

    AccountingInput input;
    input.generations = GenerationBundle::initial();
    input.policy = cca_test::relaxed_policy();
    input.policy.generation = input.generations.policy;
    input.policy.epoch = input.generations.epoch;
    input.equipment_classes.push_back(cca_test::make_class_for_property_tests());
    input.scopes.push_back(cca_test::make_scope_for_property_tests(
        "site.p", ScopeKind::Site, "", Medium::Air));
    input.scopes.push_back(cca_test::make_scope_for_property_tests(
        "loop.p", ScopeKind::Loop, "site.p", Medium::Air));

    Contribution contribution;
    contribution.id = cca_test::id_of<ContributionId>("c.derate");
    contribution.home_scope = cca_test::id_of<ScopeId>("loop.p");
    contribution.equipment = cca_test::id_of<EquipmentId>("equipment.derate");
    contribution.equipment_class =
        cca_test::id_of<EquipmentClassId>(cca_test::property_class_id());
    contribution.medium = Medium::Air;
    contribution.classification = ContributionClass::Additive;
    contribution.installed =
        Measure<ThermalPower>::known(ThermalPower::of_milliwatts(installed));
    contribution.service = ServiceState::Degraded;
    const std::uint32_t factors[3] = {first, second, third};
    for (std::uint32_t index = 0; index < 3U; ++index) {
      DerateFactor derate;
      derate.id = cca_test::id_of<DerateId>("derate." + std::to_string(index));
      derate.kind = DerateKind::Factor;
      derate.factor = Ratio::of_ppm(factors[index]).value();
      contribution.derates.push_back(derate);
    }
    contribution.binding =
        EvidenceBinding{input.generations.epoch, input.generations.topology,
                        input.generations.policy, input.generations.evidence};
    contribution.observed_at = cca_test::fixture_ago(1);
    input.contributions.push_back(contribution);

    CCA_ASSIGN(ledger, AccountingLedger::build(input, cca_test::fixture_now()));
    const ScopeAccounting* scope = ledger.find_scope(cca_test::id_of<ScopeId>("loop.p"));
    CCA_CHECK(scope != nullptr);
    if (scope == nullptr) {
      return;
    }
    // Exact expectation: floor(installed * f1 * f2 * f3 / 1000000^3). The
    // product needs more than 64 bits, so it is formed in the documented
    // 128-bit domain using the primitive that test_wide pins independently
    // against hand-computed products and limits.
    WideUInt128 numerator = wide_multiply(static_cast<std::uint64_t>(installed), 1U);
    const std::uint32_t factors_for_expectation[3] = {first, second, third};
    for (const std::uint32_t factor : factors_for_expectation) {
      CCA_ASSIGN(scaled, wide_mul_u64(numerator, static_cast<std::uint64_t>(factor)));
      numerator = scaled;
    }
    const std::uint64_t denominator = 1'000'000ULL * 1'000'000ULL * 1'000'000ULL;
    CCA_ASSIGN(expected, wide_div_u64(numerator, denominator));
    if (static_cast<std::uint64_t>(scope->totals.allocatable.milliwatts()) != expected) {
      cca_test::note("seed " + std::to_string(seed) + " installed=" +
                     std::to_string(installed) + " factors=" + std::to_string(first) +
                     "," + std::to_string(second) + "," + std::to_string(third) +
                     " ledger=" +
                     std::to_string(scope->totals.allocatable.milliwatts()) +
                     " expected=" + std::to_string(expected));
    }
    CCA_CHECK_EQ(scope->totals.allocatable.milliwatts(),
                 static_cast<std::int64_t>(expected));
    CCA_CHECK_EQ(scope->totals.degraded_loss.milliwatts(),
                 installed - static_cast<std::int64_t>(expected));
    CCA_CHECK(scope->totals.verify(scope->declared_installed).ok());
  }
}

CCA_TEST(property_canonical_form_round_trips_random_inputs) {
  for (std::uint64_t seed = 501; seed <= 515U; ++seed) {
    SeededRandom random(seed);
    const std::size_t contributions = 1U + (random.next_u32() % 30U);
    AccountingInput input = random_additive_facility(random, contributions);
    shuffle_input(input, random);
    CCA_ASSIGN(bytes, encode_canonical(input));
    CCA_ASSIGN(decoded, decode_canonical(bytes));
    CCA_ASSIGN(reencoded, encode_canonical(decoded));
    CCA_CHECK(reencoded == bytes);
    CCA_ASSIGN(direct, canonical_digest(input));
    CCA_ASSIGN(indirect, canonical_digest(decoded));
    CCA_CHECK(direct == indirect);
  }
}

CCA_TEST(property_apportionment_parts_always_resum_to_the_whole) {
  for (std::uint64_t seed = 601; seed <= 620U; ++seed) {
    SeededRandom random(seed);
    const std::int64_t installed = random.next_range(0, 1'000'000'000);
    const std::uint32_t share_a = random.next_u32() % 500'001U;
    const std::uint32_t share_b = random.next_u32() % (1'000'000U - share_a + 1U);

    AccountingInput input = random_additive_facility(random, 0U);
    input.scopes.push_back(cca_test::make_scope_for_property_tests(
        "loop.p9", ScopeKind::Loop, "zone.p0", Medium::Air));
    input.scopes.push_back(cca_test::make_scope_for_property_tests(
        "loop.p8", ScopeKind::Loop, "zone.p0", Medium::Air));

    Contribution contribution;
    contribution.id = cca_test::id_of<ContributionId>("c.shared");
    contribution.home_scope = cca_test::id_of<ScopeId>("loop.p0");
    contribution.equipment = cca_test::id_of<EquipmentId>("equipment.shared");
    contribution.equipment_class =
        cca_test::id_of<EquipmentClassId>(cca_test::property_class_id());
    contribution.medium = Medium::Air;
    contribution.classification = ContributionClass::Additive;
    contribution.installed =
        Measure<ThermalPower>::known(ThermalPower::of_milliwatts(installed));
    contribution.service = ServiceState::InService;
    contribution.sharing.kind = Sharing::Kind::Apportioned;
    contribution.sharing.shares.push_back(ApportionmentShare{
        cca_test::id_of<ScopeId>("loop.p8"), Ratio::of_ppm(share_a).value()});
    contribution.sharing.shares.push_back(ApportionmentShare{
        cca_test::id_of<ScopeId>("loop.p9"), Ratio::of_ppm(share_b).value()});
    contribution.binding =
        EvidenceBinding{input.generations.epoch, input.generations.topology,
                        input.generations.policy, input.generations.evidence};
    contribution.observed_at = cca_test::fixture_ago(1);
    input.contributions.push_back(contribution);

    CCA_ASSIGN(ledger, AccountingLedger::build(input, cca_test::fixture_now()));
    std::int64_t total = 0;
    for (const ScopeId& scope : {cca_test::id_of<ScopeId>("loop.p0"),
                                 cca_test::id_of<ScopeId>("loop.p8"),
                                 cca_test::id_of<ScopeId>("loop.p9")}) {
      const ScopeAccounting* cell = ledger.find_scope(scope);
      CCA_CHECK(cell != nullptr);
      if (cell != nullptr) {
        total += cell->declared_installed.milliwatts();
      }
    }
    CCA_CHECK_EQ(total, installed);
    CCA_CHECK(ledger.verify_closure().exact);
  }
}
