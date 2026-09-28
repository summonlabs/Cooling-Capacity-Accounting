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

// Proof obligations for the identity layer: every strong identifier tag obeys
// the same validation and ordering contract, every counter has its documented
// initial value and refuses to wrap or to run before its initial value, wide
// identifiers round trip through their canonical hexadecimal form, and the
// generation comparison is a total order inside one epoch and incomparable
// across epochs.

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

namespace {

using cooling_capacity_accounting::AccountGeneration;
using cooling_capacity_accounting::AccountGenerationTag;
using cooling_capacity_accounting::CommitSequence;
using cooling_capacity_accounting::CommitSequenceTag;
using cooling_capacity_accounting::compare_generations;
using cooling_capacity_accounting::ContributionGroupId;
using cooling_capacity_accounting::ContributionId;
using cooling_capacity_accounting::ControlPlaneEpoch;
using cooling_capacity_accounting::ControlPlaneEpochTag;
using cooling_capacity_accounting::DerateId;
using cooling_capacity_accounting::Digest;
using cooling_capacity_accounting::EquipmentClassId;
using cooling_capacity_accounting::EquipmentId;
using cooling_capacity_accounting::ErrorCode;
using cooling_capacity_accounting::EvidenceGeneration;
using cooling_capacity_accounting::EvidenceGenerationTag;
using cooling_capacity_accounting::EvidenceId;
using cooling_capacity_accounting::EvidenceSourceId;
using cooling_capacity_accounting::GenerationBundle;
using cooling_capacity_accounting::generation_order_name;
using cooling_capacity_accounting::GenerationOrder;
using cooling_capacity_accounting::Identifier;
using cooling_capacity_accounting::IndependenceDomainId;
using cooling_capacity_accounting::kMaxIdentifierLength;
using cooling_capacity_accounting::LoopId;
using cooling_capacity_accounting::ManifestId;
using cooling_capacity_accounting::ObservationSequence;
using cooling_capacity_accounting::ObservationSequenceTag;
using cooling_capacity_accounting::PolicyGeneration;
using cooling_capacity_accounting::PolicyGenerationTag;
using cooling_capacity_accounting::PolicyId;
using cooling_capacity_accounting::ScopeId;
using cooling_capacity_accounting::SiteId;
using cooling_capacity_accounting::StateRevision;
using cooling_capacity_accounting::StateRevisionTag;
using cooling_capacity_accounting::to_decimal;
using cooling_capacity_accounting::TopologyGeneration;
using cooling_capacity_accounting::TopologyGenerationTag;
using cooling_capacity_accounting::WideId;
using cooling_capacity_accounting::ZoneId;

constexpr std::uint64_t kU64Max = std::numeric_limits<std::uint64_t>::max();

// Every identifier alias carries its own tag type, so two identity domains can
// never be interchanged by accident.
static_assert(std::is_same_v<SiteId, cooling_capacity_accounting::StrongIdentifier<
                                         cooling_capacity_accounting::SiteTag>>);
static_assert(std::is_same_v<SiteId::tag_type, cooling_capacity_accounting::SiteTag>);
static_assert(std::is_same_v<ZoneId::tag_type, cooling_capacity_accounting::ZoneTag>);
static_assert(std::is_same_v<LoopId::tag_type, cooling_capacity_accounting::LoopTag>);
static_assert(std::is_same_v<EquipmentId::tag_type,
                             cooling_capacity_accounting::EquipmentTag>);
static_assert(std::is_same_v<EquipmentClassId::tag_type,
                             cooling_capacity_accounting::EquipmentClassTag>);
static_assert(std::is_same_v<ScopeId::tag_type, cooling_capacity_accounting::ScopeTag>);
static_assert(std::is_same_v<ContributionId::tag_type,
                             cooling_capacity_accounting::ContributionTag>);
static_assert(std::is_same_v<ContributionGroupId::tag_type,
                             cooling_capacity_accounting::ContributionGroupTag>);
static_assert(std::is_same_v<IndependenceDomainId::tag_type,
                             cooling_capacity_accounting::IndependenceDomainTag>);
static_assert(std::is_same_v<EvidenceId::tag_type,
                             cooling_capacity_accounting::EvidenceTag>);
static_assert(std::is_same_v<EvidenceSourceId::tag_type,
                             cooling_capacity_accounting::EvidenceSourceTag>);
static_assert(std::is_same_v<DerateId::tag_type, cooling_capacity_accounting::DerateTag>);
static_assert(std::is_same_v<PolicyId::tag_type, cooling_capacity_accounting::PolicyTag>);
static_assert(std::is_same_v<ManifestId::tag_type,
                             cooling_capacity_accounting::ManifestTag>);
static_assert(!std::is_same_v<SiteId, ZoneId>);
static_assert(!std::is_same_v<ContributionId, EvidenceId>);
static_assert(!std::is_same_v<StateRevision, CommitSequence>);
static_assert(!std::is_same_v<ControlPlaneEpoch, TopologyGeneration>);

/// The whole validated-identifier contract, instantiated once per tag.
template <typename Id>
void check_strong_identifier_contract() {
  CCA_ASSIGN(parsed, Id::parse("unit.alpha-1"));
  CCA_CHECK_EQ(parsed.str(), std::string("unit.alpha-1"));
  CCA_CHECK_EQ(parsed.view(), std::string_view("unit.alpha-1"));
  CCA_CHECK_EQ(parsed.value().str(), std::string("unit.alpha-1"));
  CCA_CHECK(!parsed.empty());

  CCA_CHECK_CODE(Id::parse(std::string_view()), ErrorCode::InvalidIdentifier);
  CCA_CHECK_CODE(Id::parse(std::string(kMaxIdentifierLength + 1U, 'z')),
                 ErrorCode::InvalidIdentifier);
  CCA_CHECK_CODE(Id::parse("bad id"), ErrorCode::InvalidIdentifier);
  CCA_CHECK_CODE(Id::parse("bad/id"), ErrorCode::InvalidIdentifier);

  const std::string at_bound(kMaxIdentifierLength, 'q');
  CCA_ASSIGN(longest, Id::parse(at_bound));
  CCA_CHECK_EQ(longest.str(), at_bound);

  const Id empty_default;
  CCA_CHECK(empty_default.empty());
  CCA_CHECK_EQ(empty_default.str(), std::string());
  CCA_CHECK(empty_default != parsed);

  const Id same = Id::from_validated(Identifier::from_validated("unit.alpha-1"));
  CCA_CHECK(parsed == same);
  CCA_CHECK(!(parsed != same));

  CCA_ASSIGN(lower, Id::parse("unit.alpha-0"));
  CCA_ASSIGN(higher, Id::parse("unit.alpha-2"));
  CCA_CHECK(lower < parsed);
  CCA_CHECK(parsed < higher);
  CCA_CHECK(lower != higher);
  CCA_CHECK(!(higher < lower));
  CCA_ASSIGN(lower_again, Id::parse("unit.alpha-0"));
  CCA_CHECK(lower == lower_again);
}

/// The counter contract, instantiated once per tag with the initial value the
/// tag fixes.
template <typename CounterType>
void check_counter_contract(std::uint64_t initial_value) {
  const CounterType initial = CounterType::initial();
  CCA_CHECK(initial.is_initial());
  CCA_CHECK_EQ(initial.value(), initial_value);

  CCA_ASSIGN(same, CounterType::of(initial_value));
  CCA_CHECK_EQ(same, initial);
  CCA_CHECK(!(same != initial));

  if (initial_value > 0U) {
    CCA_CHECK_CODE(CounterType::of(0U), ErrorCode::OutOfRange);
    CCA_CHECK_CODE(CounterType::of(initial_value - 1U), ErrorCode::OutOfRange);
  } else {
    CCA_CHECK(CounterType::of(0U).ok());
  }

  CCA_ASSIGN(next, initial.next());
  CCA_CHECK_EQ(next.value(), initial_value + 1U);
  CCA_CHECK(next > initial);
  CCA_CHECK(next != initial);
  CCA_CHECK(initial < next);
  CCA_CHECK(initial <= next);
  CCA_CHECK(next >= initial);

  CCA_ASSIGN(parsed, CounterType::parse(to_decimal(initial_value)));
  CCA_CHECK_EQ(parsed, initial);
  CCA_ASSIGN(parsed_next, CounterType::parse(to_decimal(next.value())));
  CCA_CHECK_EQ(parsed_next, next);
  CCA_CHECK_CODE(CounterType::parse(std::string_view()), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(CounterType::parse("abc"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(CounterType::parse("-1"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(CounterType::parse("01"), ErrorCode::InvalidNumber);
  if (initial_value > 0U) {
    CCA_CHECK_CODE(CounterType::parse("0"), ErrorCode::OutOfRange);
  }

  // next() refuses to wrap at the top of the range.
  CCA_ASSIGN(top, CounterType::of(kU64Max));
  CCA_CHECK_EQ(top.value(), kU64Max);
  CCA_CHECK_CODE(top.next(), ErrorCode::NumericOverflow);
  CCA_ASSIGN(almost, CounterType::of(kU64Max - 1U));
  CCA_ASSIGN(advanced, almost.next());
  CCA_CHECK_EQ(advanced.value(), kU64Max);
  CCA_CHECK_EQ(advanced, top);
}

}  // namespace

CCA_TEST(strong_identifier_contract_holds_for_every_tag) {
  check_strong_identifier_contract<SiteId>();
  check_strong_identifier_contract<ZoneId>();
  check_strong_identifier_contract<LoopId>();
  check_strong_identifier_contract<EquipmentId>();
  check_strong_identifier_contract<EquipmentClassId>();
  check_strong_identifier_contract<ScopeId>();
  check_strong_identifier_contract<ContributionId>();
  check_strong_identifier_contract<ContributionGroupId>();
  check_strong_identifier_contract<IndependenceDomainId>();
  check_strong_identifier_contract<EvidenceId>();
  check_strong_identifier_contract<EvidenceSourceId>();
  check_strong_identifier_contract<DerateId>();
  check_strong_identifier_contract<PolicyId>();
  check_strong_identifier_contract<ManifestId>();
  CCA_CHECK(true);
}

CCA_TEST(counter_initial_values_are_fixed_by_tag) {
  CCA_CHECK_EQ(ControlPlaneEpochTag::kInitial, 1U);
  CCA_CHECK_EQ(TopologyGenerationTag::kInitial, 1U);
  CCA_CHECK_EQ(PolicyGenerationTag::kInitial, 1U);
  CCA_CHECK_EQ(EvidenceGenerationTag::kInitial, 1U);
  CCA_CHECK_EQ(AccountGenerationTag::kInitial, 1U);
  CCA_CHECK_EQ(StateRevisionTag::kInitial, 0U);
  CCA_CHECK_EQ(CommitSequenceTag::kInitial, 0U);
  CCA_CHECK_EQ(ObservationSequenceTag::kInitial, 0U);

  CCA_CHECK_EQ(ControlPlaneEpoch::initial().value(), 1U);
  CCA_CHECK_EQ(TopologyGeneration::initial().value(), 1U);
  CCA_CHECK_EQ(PolicyGeneration::initial().value(), 1U);
  CCA_CHECK_EQ(EvidenceGeneration::initial().value(), 1U);
  CCA_CHECK_EQ(AccountGeneration::initial().value(), 1U);
  CCA_CHECK_EQ(StateRevision::initial().value(), 0U);
  CCA_CHECK_EQ(CommitSequence::initial().value(), 0U);
  CCA_CHECK_EQ(ObservationSequence::initial().value(), 0U);

  CCA_CHECK(ControlPlaneEpoch::initial().is_initial());
  CCA_CHECK(TopologyGeneration::initial().is_initial());
  CCA_CHECK(PolicyGeneration::initial().is_initial());
  CCA_CHECK(EvidenceGeneration::initial().is_initial());
  CCA_CHECK(AccountGeneration::initial().is_initial());
  CCA_CHECK(StateRevision::initial().is_initial());
  CCA_CHECK(CommitSequence::initial().is_initial());
  CCA_CHECK(ObservationSequence::initial().is_initial());

  // Two counters of different tags with the same numeric value are distinct
  // types and compare only within their own tag.
  CCA_ASSIGN(revision_zero, StateRevision::of(0U));
  CCA_ASSIGN(commit_zero, CommitSequence::of(0U));
  CCA_CHECK_EQ(revision_zero.value(), commit_zero.value());
  CCA_CHECK(revision_zero == StateRevision::initial());
  CCA_CHECK(commit_zero == CommitSequence::initial());
}

CCA_TEST(counter_contract_holds_for_every_tag) {
  check_counter_contract<ControlPlaneEpoch>(1U);
  check_counter_contract<TopologyGeneration>(1U);
  check_counter_contract<PolicyGeneration>(1U);
  check_counter_contract<EvidenceGeneration>(1U);
  check_counter_contract<AccountGeneration>(1U);
  check_counter_contract<StateRevision>(0U);
  check_counter_contract<CommitSequence>(0U);
  check_counter_contract<ObservationSequence>(0U);
}

CCA_TEST(wide_id_from_material_is_deterministic) {
  CCA_ASSIGN(first, WideId::from_material("attempt-1"));
  CCA_ASSIGN(repeated, WideId::from_material("attempt-1"));
  CCA_CHECK_EQ(first, repeated);
  CCA_CHECK(!(first != repeated));
  CCA_CHECK_EQ(first.to_string(), repeated.to_string());

  CCA_ASSIGN(second, WideId::from_material("attempt-2"));
  CCA_CHECK(first != second);
  CCA_CHECK(first.to_string() != second.to_string());

  // The wide identifier is the first sixteen digest bytes, big endian.
  const Digest digest = Digest::of_text("attempt-1");
  const WideId from_digest = WideId::from_digest(digest);
  CCA_CHECK_EQ(first, from_digest);
  CCA_CHECK_EQ(first.to_string(), digest.to_hex().substr(0U, 32U));

  // Empty material is material: it hashes like every other byte string.
  CCA_ASSIGN(empty_material, WideId::from_material(std::string_view()));
  const WideId empty_digest = WideId::from_digest(Digest::of_text(std::string_view()));
  CCA_CHECK_EQ(empty_material, empty_digest);
  CCA_CHECK(empty_material != first);

  // Materials that differ only in length or ordering are different identities.
  CCA_ASSIGN(ab, WideId::from_material("ab"));
  CCA_ASSIGN(ba, WideId::from_material("ba"));
  CCA_CHECK(ab != ba);
  CCA_ASSIGN(a, WideId::from_material("a"));
  CCA_ASSIGN(aa, WideId::from_material("aa"));
  CCA_CHECK(a != aa);
}

CCA_TEST(wide_id_parse_and_to_string_round_trip) {
  CCA_ASSIGN(lower, WideId::parse("0123456789abcdef0123456789abcdef"));
  CCA_CHECK_EQ(lower.high(), 0x0123456789abcdefULL);
  CCA_CHECK_EQ(lower.low(), 0x0123456789abcdefULL);
  CCA_CHECK_EQ(lower.to_string(), std::string("0123456789abcdef0123456789abcdef"));
  CCA_ASSIGN(round_trip, WideId::parse(lower.to_string()));
  CCA_CHECK_EQ(round_trip, lower);

  // Hexadecimal digits are accepted in either case and rendered lower case.
  CCA_ASSIGN(upper, WideId::parse("0123456789ABCDEF0123456789ABCDEF"));
  CCA_CHECK_EQ(upper, lower);
  CCA_CHECK_EQ(upper.to_string(), lower.to_string());

  CCA_ASSIGN(maximum, WideId::parse("ffffffffffffffffffffffffffffffff"));
  CCA_CHECK_EQ(maximum.high(), kU64Max);
  CCA_CHECK_EQ(maximum.low(), kU64Max);
  CCA_CHECK_EQ(maximum.to_string(),
               std::string("ffffffffffffffffffffffffffffffff"));
  CCA_ASSIGN(maximum_upper, WideId::parse("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"));
  CCA_CHECK_EQ(maximum_upper, maximum);

  CCA_ASSIGN(zero, WideId::parse("00000000000000000000000000000000"));
  CCA_CHECK_EQ(zero.high(), 0U);
  CCA_CHECK_EQ(zero.low(), 0U);
  CCA_CHECK_EQ(zero, WideId{});
  CCA_CHECK_EQ(zero.to_string(), std::string("00000000000000000000000000000000"));

  // Exactly 32 hexadecimal characters, nothing else.
  CCA_CHECK_CODE(WideId::parse(std::string_view()), ErrorCode::InvalidIdentifier);
  CCA_CHECK_CODE(WideId::parse(std::string(31U, 'a')), ErrorCode::InvalidIdentifier);
  CCA_CHECK_CODE(WideId::parse(std::string(33U, 'a')), ErrorCode::InvalidIdentifier);
  CCA_CHECK_CODE(WideId::parse("0123456789abcdef0123456789abcdeg"),
                 ErrorCode::InvalidIdentifier);
  CCA_CHECK_CODE(WideId::parse("0123456789abcdef0123456789abcde "),
                 ErrorCode::InvalidIdentifier);
  CCA_CHECK_CODE(WideId::parse("0123456789abcdef-123456789abcdef"),
                 ErrorCode::InvalidIdentifier);

  // Every material-derived identifier survives the text round trip.
  for (const std::string_view material : {"", "a", "attempt-1", "attempt-2",
                                          "the quick brown fox"}) {
    CCA_ASSIGN(id, WideId::from_material(material));
    CCA_CHECK_EQ(id.to_string().size(), 32U);
    for (const char byte : id.to_string()) {
      const bool hexadecimal = (byte >= '0' && byte <= '9') ||
                               (byte >= 'a' && byte <= 'f');
      if (!hexadecimal) {
        CCA_FAIL("to_string produced a byte that is not lower-case hexadecimal");
        return;
      }
    }
    CCA_ASSIGN(parsed, WideId::parse(id.to_string()));
    CCA_CHECK_EQ(parsed, id);
  }
}

CCA_TEST(compare_generations_is_ordered_within_an_epoch) {
  const GenerationBundle initial = GenerationBundle::initial();
  CCA_CHECK_EQ(compare_generations(initial, initial), GenerationOrder::Equal);
  CCA_CHECK_EQ(compare_generations(GenerationBundle::initial(), initial),
               GenerationOrder::Equal);

  GenerationBundle newer_revision = initial;
  CCA_ASSIGN(revision_two, StateRevision::of(2U));
  newer_revision.revision = revision_two;
  CCA_CHECK_EQ(compare_generations(initial, newer_revision), GenerationOrder::Older);
  CCA_CHECK_EQ(compare_generations(newer_revision, initial), GenerationOrder::Newer);

  GenerationBundle newer_evidence = initial;
  CCA_ASSIGN(evidence_two, EvidenceGeneration::of(2U));
  newer_evidence.evidence = evidence_two;
  CCA_CHECK_EQ(compare_generations(initial, newer_evidence), GenerationOrder::Older);
  CCA_CHECK_EQ(compare_generations(newer_evidence, initial), GenerationOrder::Newer);
  CCA_CHECK(compare_generations(newer_evidence, newer_revision) !=
            GenerationOrder::Equal);

  GenerationBundle newer_policy = initial;
  CCA_ASSIGN(policy_two, PolicyGeneration::of(2U));
  newer_policy.policy = policy_two;
  CCA_CHECK_EQ(compare_generations(initial, newer_policy), GenerationOrder::Older);

  GenerationBundle newer_topology = initial;
  CCA_ASSIGN(topology_two, TopologyGeneration::of(2U));
  newer_topology.topology = topology_two;
  CCA_CHECK_EQ(compare_generations(initial, newer_topology), GenerationOrder::Older);

  // The comparison is lexicographic by (topology, policy, evidence, revision),
  // so a newer topology outranks an older revision.
  GenerationBundle mixed = newer_topology;
  mixed.revision = StateRevision::initial();
  CCA_CHECK_EQ(compare_generations(newer_revision, mixed), GenerationOrder::Older);
  CCA_CHECK_EQ(compare_generations(mixed, newer_revision), GenerationOrder::Newer);

  // A newer revision never outranks an older topology.
  GenerationBundle older_topology_newer_revision = initial;
  CCA_ASSIGN(revision_five, StateRevision::of(5U));
  older_topology_newer_revision.revision = revision_five;
  CCA_CHECK_EQ(compare_generations(older_topology_newer_revision, newer_topology),
               GenerationOrder::Older);
}

CCA_TEST(compare_generations_is_incomparable_across_epochs) {
  const GenerationBundle initial = GenerationBundle::initial();
  GenerationBundle other_epoch = initial;
  CCA_ASSIGN(epoch_two, ControlPlaneEpoch::of(2U));
  other_epoch.epoch = epoch_two;
  CCA_CHECK_EQ(compare_generations(initial, other_epoch),
               GenerationOrder::Incomparable);
  CCA_CHECK_EQ(compare_generations(other_epoch, initial),
               GenerationOrder::Incomparable);

  // Even when every other field is also newer, the epochs still dominate.
  CCA_ASSIGN(topology_two, TopologyGeneration::of(2U));
  CCA_ASSIGN(revision_two, StateRevision::of(2U));
  other_epoch.topology = topology_two;
  other_epoch.revision = revision_two;
  CCA_CHECK_EQ(compare_generations(initial, other_epoch),
               GenerationOrder::Incomparable);
  CCA_CHECK_EQ(compare_generations(other_epoch, initial),
               GenerationOrder::Incomparable);

  // An older epoch is equally incomparable with a newer one.
  CCA_ASSIGN(epoch_three, ControlPlaneEpoch::of(3U));
  other_epoch.epoch = epoch_three;
  GenerationBundle epoch_two_bundle = initial;
  epoch_two_bundle.epoch = epoch_two;
  CCA_CHECK_EQ(compare_generations(epoch_two_bundle, other_epoch),
               GenerationOrder::Incomparable);
}

CCA_TEST(generation_order_names) {
  CCA_CHECK_EQ(generation_order_name(GenerationOrder::Equal), std::string_view("Equal"));
  CCA_CHECK_EQ(generation_order_name(GenerationOrder::Older), std::string_view("Older"));
  CCA_CHECK_EQ(generation_order_name(GenerationOrder::Newer), std::string_view("Newer"));
  CCA_CHECK_EQ(generation_order_name(GenerationOrder::Incomparable),
               std::string_view("Incomparable"));
  CCA_CHECK(generation_order_name(GenerationOrder::Equal) !=
            generation_order_name(GenerationOrder::Older));
}
