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

// Canonical byte form: round trips, order independence, byte identity and the
// exact refusal of every malformed shape the format defines.

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fixtures.hpp"
#include "test_harness.hpp"

using namespace cooling_capacity_accounting;
using cca_test::SeededRandom;
using cca_test::fixture_ago;
using cca_test::fixture_now;
using cca_test::id_of;
using cca_test::note;

namespace {

// ---------------------------------------------------------------------------
// Fixture builders
// ---------------------------------------------------------------------------

BoundedText text_of(std::string_view value) {
  return BoundedText::from_validated(std::string(value));
}

EvidenceBinding binding_of(const GenerationBundle& generations) {
  return EvidenceBinding{generations.epoch, generations.topology, generations.policy,
                         generations.evidence};
}

/// A rich input with at least one record of every kind, every optional field
/// present, and several of the record vectors holding more than one element.
[[nodiscard]] AccountingInput rich_input() {
  AccountingInput input;
  input.generations.epoch = ControlPlaneEpoch::of(3).value();
  input.generations.topology = TopologyGeneration::of(4).value();
  input.generations.policy = PolicyGeneration::of(5).value();
  input.generations.evidence = EvidenceGeneration::of(6).value();
  input.generations.revision = StateRevision::of(7).value();

  AccountingPolicy& policy = input.policy;
  policy.id = id_of<PolicyId>("cca.policy.rich");
  policy.generation = input.generations.policy;
  policy.epoch = input.generations.epoch;
  policy.effective_from = fixture_ago(7'200'000);
  policy.max_evidence_age = DurationMs::of_milliseconds(3'600'000);
  policy.require_out_of_service_evidence = true;
  policy.require_classification = true;
  policy.require_installed_evidence = false;
  policy.require_derate_evidence = true;
  policy.minimum_coverage = Ratio::of_ppm(900'000).value();
  policy.residual_tolerance = Ratio::of_ppm(1'000).value();
  policy.max_derate_factors = 3;
  policy.retention_generations = 8;
  policy.note = text_of("rich policy note");

  EquipmentClass chiller;
  chiller.id = id_of<EquipmentClassId>("class.chiller");
  chiller.kind = EquipmentClassKind::Chiller;
  chiller.medium = Medium::Liquid;
  chiller.label = text_of("water cooled chiller");
  chiller.contributes_to_installed = false;
  input.equipment_classes.push_back(chiller);

  EquipmentClass crac;
  crac.id = id_of<EquipmentClassId>("class.crah");
  crac.kind = EquipmentClassKind::ComputerRoomAirHandler;
  crac.medium = Medium::Air;
  crac.label = text_of("computer room air handler");
  crac.contributes_to_installed = true;
  input.equipment_classes.push_back(crac);

  EvidenceRecord nameplate;
  nameplate.id = id_of<EvidenceId>("evidence.alpha");
  nameplate.kind = EvidenceKind::Nameplate;
  nameplate.source_kind = EvidenceSourceKind::VendorDocument;
  nameplate.source = id_of<EvidenceSourceId>("source.vendor");
  nameplate.label = text_of("alpha nameplate");
  nameplate.reference = DocumentRef::parse("doc://alpha/nameplate").value();
  nameplate.observed_at = fixture_ago(3'600'000);
  nameplate.recorded_at = fixture_ago(3'500'000);
  nameplate.sequence = ObservationSequence::of(9).value();
  nameplate.binding = binding_of(input.generations);
  nameplate.content_digest = Digest::of_text("alpha");
  nameplate.medium = Medium::Air;
  nameplate.subject_equipment = id_of<EquipmentId>("equipment.alpha");
  nameplate.subject_scope = id_of<ScopeId>("loop.a.0");
  nameplate.declared_value =
      Measure<ThermalPower>::known(ThermalPower::of_milliwatts(250'000));
  nameplate.notes = text_of("alpha notes");
  input.evidence.push_back(nameplate);

  EvidenceRecord silent;
  silent.id = id_of<EvidenceId>("evidence.beta");
  silent.kind = EvidenceKind::SensorReading;
  silent.source_kind = EvidenceSourceKind::BmsExport;
  silent.source = id_of<EvidenceSourceId>("source.bms");
  silent.label = text_of("beta sensor");
  silent.observed_at = fixture_ago(1'800'000);
  silent.recorded_at = fixture_ago(1'700'000);
  silent.binding = binding_of(input.generations);
  silent.content_digest = Digest::of_text("beta");
  silent.medium = Medium::Liquid;
  silent.declared_value = Measure<ThermalPower>::unknown(MeasureReason::SensorAbsent,
                                                         text_of("sensor absent"));
  input.evidence.push_back(silent);

  EvidenceRecord unsupported;
  unsupported.id = id_of<EvidenceId>("evidence.gamma");
  unsupported.kind = EvidenceKind::OperatorDeclaration;
  unsupported.source_kind = EvidenceSourceKind::Operator;
  unsupported.source = id_of<EvidenceSourceId>("source.operator");
  unsupported.label = text_of("gamma declaration");
  unsupported.observed_at = fixture_ago(600'000);
  unsupported.recorded_at = fixture_ago(500'000);
  unsupported.sequence = ObservationSequence::of(3).value();
  unsupported.binding = binding_of(input.generations);
  unsupported.content_digest = Digest::of_text("gamma");
  unsupported.medium = Medium::Air;
  unsupported.declared_value = Measure<ThermalPower>::unsupported(
      MeasureReason::NotCommissioned, text_of("not commissioned"));
  input.evidence.push_back(unsupported);

  IndependenceDomain pdu_a;
  pdu_a.id = id_of<IndependenceDomainId>("domain.pdu-a");
  pdu_a.label = text_of("PDU A");
  pdu_a.evidence = id_of<EvidenceId>("evidence.beta");
  pdu_a.scope = id_of<ScopeId>("site.alpha");
  pdu_a.topology_generation = input.generations.topology;
  pdu_a.epoch = input.generations.epoch;
  input.independence_domains.push_back(pdu_a);

  IndependenceDomain pdu_b;
  pdu_b.id = id_of<IndependenceDomainId>("domain.pdu-b");
  pdu_b.label = text_of("PDU B");
  pdu_b.scope = id_of<ScopeId>("site.alpha");
  pdu_b.topology_generation = input.generations.topology;
  pdu_b.epoch = input.generations.epoch;
  input.independence_domains.push_back(pdu_b);

  AccountingScope site;
  site.id = id_of<ScopeId>("site.alpha");
  site.kind = ScopeKind::Site;
  site.label = text_of("synthetic site");
  site.medium = Medium::Air;
  site.classes.push_back(id_of<EquipmentClassId>("class.chiller"));
  site.classes.push_back(id_of<EquipmentClassId>("class.crah"));
  site.topology_generation = input.generations.topology;
  site.epoch = input.generations.epoch;
  site.declared_installed_total =
      Measure<ThermalPower>::known(ThermalPower::of_milliwatts(1'000'000));
  site.declared_total_evidence.push_back(id_of<EvidenceId>("evidence.alpha"));
  input.scopes.push_back(site);

  AccountingScope loop;
  loop.id = id_of<ScopeId>("loop.a.0");
  loop.kind = ScopeKind::Loop;
  loop.parent = id_of<ScopeId>("zone.a");
  loop.label = text_of("loop a 0");
  loop.medium = Medium::Liquid;
  loop.classes.push_back(id_of<EquipmentClassId>("class.chiller"));
  loop.topology_generation = input.generations.topology;
  loop.epoch = input.generations.epoch;
  input.scopes.push_back(loop);

  AccountingScope zone;
  zone.id = id_of<ScopeId>("zone.a");
  zone.kind = ScopeKind::Zone;
  zone.parent = id_of<ScopeId>("site.alpha");
  zone.label = text_of("zone a");
  zone.medium = Medium::Air;
  zone.topology_generation = input.generations.topology;
  zone.epoch = input.generations.epoch;
  zone.declared_installed_total =
      Measure<ThermalPower>::unknown(MeasureReason::NotMeasured, BoundedText());
  zone.declared_total_evidence.push_back(id_of<EvidenceId>("evidence.gamma"));
  input.scopes.push_back(zone);

  AccountingScope band;
  band.id = id_of<ScopeId>("band.a.0");
  band.kind = ScopeKind::ClassBand;
  band.parent = id_of<ScopeId>("loop.a.0");
  band.label = text_of("class band a 0");
  band.medium = Medium::Liquid;
  band.classes.push_back(id_of<EquipmentClassId>("class.chiller"));
  band.topology_generation = input.generations.topology;
  band.epoch = input.generations.epoch;
  input.scopes.push_back(band);

  ContributionGroup group;
  group.id = id_of<ContributionGroupId>("group.chillers");
  group.scope = id_of<ScopeId>("loop.a.0");
  group.classification = ContributionClass::Redundant;
  group.medium = Medium::Liquid;
  group.required_concurrent = 2;
  group.redundancy = RedundancyClass::NPlus1;
  group.protected_quantity =
      Measure<ThermalPower>::known(ThermalPower::of_milliwatts(400'000));
  group.protected_quantity_evidence = id_of<EvidenceId>("evidence.alpha");
  group.independence_domains.push_back(id_of<IndependenceDomainId>("domain.pdu-a"));
  group.independence_domains.push_back(id_of<IndependenceDomainId>("domain.pdu-b"));
  group.binding = binding_of(input.generations);
  input.groups.push_back(group);

  Contribution additive = cca_test::make_contribution(
      "contribution.alpha", "loop.a.0", "class.crah", "equipment.alpha", 250'000,
      input.generations);
  additive.primary_evidence = id_of<EvidenceId>("evidence.alpha");
  additive.evidence.push_back(id_of<EvidenceId>("evidence.beta"));
  additive.evidence.push_back(id_of<EvidenceId>("evidence.gamma"));
  additive.aliases.push_back(id_of<ScopeId>("zone.a"));
  additive.observed_at = fixture_ago(3'000'000);
  additive.observation_sequence = ObservationSequence::of(11).value();
  input.contributions.push_back(additive);

  Contribution degraded = cca_test::make_contribution(
      "contribution.bravo", "loop.a.0", "class.chiller", "equipment.bravo", 900'000,
      input.generations);
  degraded.service = ServiceState::Degraded;
  degraded.classification = ContributionClass::Substitutive;
  degraded.group = id_of<ContributionGroupId>("group.chillers");
  degraded.independence_domain = id_of<IndependenceDomainId>("domain.pdu-a");
  degraded.priority = 3;
  degraded.derates.push_back(DerateFactor(id_of<DerateId>("derate.bravo.absolute"),
                                          ThermalPower::of_milliwatts(50'000)));
  degraded.derates.push_back(
      DerateFactor(id_of<DerateId>("derate.bravo.factor"), Ratio::of_ppm(850'000).value()));
  degraded.sharing.kind = Sharing::Kind::Apportioned;
  degraded.sharing.shares.push_back(
      ApportionmentShare{id_of<ScopeId>("band.a.0"), Ratio::of_ppm(250'000).value()});
  degraded.sharing.shares.push_back(
      ApportionmentShare{id_of<ScopeId>("site.alpha"), Ratio::of_ppm(100'000).value()});
  degraded.evidence.push_back(id_of<EvidenceId>("evidence.beta"));
  degraded.observed_at = fixture_ago(1'000'000);
  input.contributions.push_back(degraded);

  Contribution standby = cca_test::make_contribution(
      "contribution.charlie", "band.a.0", "class.chiller", "equipment.charlie", 10'000,
      input.generations);
  standby.installed =
      Measure<ThermalPower>::unknown(MeasureReason::InventoryUnknown, text_of("no list"));
  standby.service = ServiceState::Unknown;
  standby.classification = ContributionClass::ReserveOnly;
  standby.observed_at = fixture_now();
  input.contributions.push_back(standby);

  ManifestDeclaration declaration;
  declaration.id = id_of<ManifestId>("manifest.alpha");
  declaration.scope = id_of<ScopeId>("site.alpha");
  declaration.equipment_class = id_of<EquipmentClassId>("class.crah");
  declaration.medium = Medium::Air;
  declaration.declared_installed = ThermalPower::of_milliwatts(2'000'000);
  declaration.evidence = id_of<EvidenceId>("evidence.alpha");
  declaration.observed_at = fixture_ago(4'000'000);
  declaration.binding = binding_of(input.generations);
  input.manifest_declarations.push_back(declaration);

  ManifestDeclaration second;
  second.id = id_of<ManifestId>("manifest.bravo");
  second.scope = id_of<ScopeId>("zone.a");
  second.medium = Medium::Air;
  second.declared_installed = ThermalPower::of_milliwatts(750'000);
  second.evidence = id_of<EvidenceId>("evidence.gamma");
  second.observed_at = fixture_ago(2'000'000);
  second.binding = binding_of(input.generations);
  input.manifest_declarations.push_back(second);

  return input;
}

// ---------------------------------------------------------------------------
// Layout of the minimal payload the mutation tests address by offset
// ---------------------------------------------------------------------------

constexpr std::size_t kHeaderBytes = 8U + 4U + 4U + 8U;
constexpr std::size_t kGenerationsBytes = 5U * 8U;
// Everything the policy record carries after the identifier text and before the
// note text: generation, epoch, effective_from, max_evidence_age, four booleans,
// minimum_coverage, residual_tolerance, max_derate_factors, retention.
constexpr std::size_t kPolicyFixedBytes = 8U + 8U + 8U + 8U + 4U + 4U + 4U + 8U + 8U;
constexpr std::size_t kClassRecordBytes = 4U + 11U + 4U + 4U + 4U + 1U;
constexpr std::size_t kTrailerBytes = 8U;

[[nodiscard]] std::size_t policy_bytes(const AccountingPolicy& policy) {
  return 4U + policy.id.str().size() + kPolicyFixedBytes + 4U + policy.note.str().size();
}

/// Two equipment classes and nothing else. Both class identifiers are the same
/// length, so the two records are the same size and their boundaries are exact.
[[nodiscard]] AccountingInput two_class_input() {
  AccountingInput input;
  input.policy.id = id_of<PolicyId>("p");

  EquipmentClass alpha;
  alpha.id = id_of<EquipmentClassId>("class.alpha");
  alpha.kind = EquipmentClassKind::Chiller;
  alpha.medium = Medium::Air;
  alpha.contributes_to_installed = true;
  input.equipment_classes.push_back(alpha);

  EquipmentClass bravo;
  bravo.id = id_of<EquipmentClassId>("class.bravo");
  bravo.kind = EquipmentClassKind::DryCooler;
  bravo.medium = Medium::Air;
  bravo.contributes_to_installed = false;
  input.equipment_classes.push_back(bravo);
  return input;
}

// ---------------------------------------------------------------------------
// Byte helpers
// ---------------------------------------------------------------------------

using Bytes = std::vector<std::uint8_t>;

[[nodiscard]] Bytes with_u32(Bytes bytes, std::size_t offset, std::uint32_t value) {
  for (std::size_t index = 0; index < 4U; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(
        (value >> ((3U - index) * 8U)) & 0xFFU);
  }
  return bytes;
}

[[nodiscard]] Bytes with_u64(Bytes bytes, std::size_t offset, std::uint64_t value) {
  for (std::size_t index = 0; index < 8U; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(
        (value >> ((7U - index) * 8U)) & 0xFFU);
  }
  return bytes;
}

[[nodiscard]] std::uint32_t read_u32(const Bytes& bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4U; ++index) {
    value = (value << 8U) | bytes[offset + index];
  }
  return value;
}

[[nodiscard]] std::uint64_t read_u64(const Bytes& bytes, std::size_t offset) {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 8U; ++index) {
    value = (value << 8U) | bytes[offset + index];
  }
  return value;
}

/// Offset of the unique length-prefixed occurrence of text. Every canonical
/// record begins with its identifier, so this is the record's first byte.
[[nodiscard]] std::size_t find_record(const Bytes& bytes, std::string_view text) {
  Bytes needle;
  const std::uint32_t length = static_cast<std::uint32_t>(text.size());
  for (std::size_t index = 0; index < 4U; ++index) {
    needle.push_back(
        static_cast<std::uint8_t>((length >> ((3U - index) * 8U)) & 0xFFU));
  }
  needle.insert(needle.end(), text.begin(), text.end());
  std::size_t found = bytes.size();
  std::size_t occurrences = 0;
  for (std::size_t index = 0; index + needle.size() <= bytes.size(); ++index) {
    if (std::equal(needle.begin(), needle.end(), bytes.data() + index)) {
      occurrences += 1;
      found = index;
    }
  }
  CCA_CHECK_EQ(occurrences, static_cast<std::size_t>(1));
  return found;
}

/// Shuffles a vector with the harness generator, so a permutation is exactly
/// reproducible from the printed seed.
template <typename T>
void shuffle_with(std::vector<T>& values, SeededRandom& random) {
  for (std::size_t index = values.size(); index > 1U; --index) {
    const auto target = static_cast<std::size_t>(
        random.next_range(0, static_cast<std::int64_t>(index) - 1));
    std::swap(values[index - 1U], values[target]);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Round trips
// ---------------------------------------------------------------------------

CCA_TEST(canonical_empty_input_round_trips) {
  // No scopes, no contributions, no evidence: still a complete, named policy.
  AccountingInput empty;
  empty.policy.id = id_of<PolicyId>("cca.policy.empty");
  CCA_ASSIGN(bytes, encode_canonical(empty));
  CCA_CHECK_EQ(bytes.size(), kHeaderBytes + kGenerationsBytes +
                                 policy_bytes(empty.policy) + 7U * 4U + kTrailerBytes);
  CCA_CHECK(std::string_view(reinterpret_cast<const char*>(bytes.data()), 8U) ==
            kCanonicalMagic);

  CCA_ASSIGN(decoded, decode_canonical(bytes));
  CCA_CHECK(decoded.scopes.empty());
  CCA_CHECK(decoded.contributions.empty());
  CCA_CHECK(decoded.groups.empty());
  CCA_CHECK(decoded.evidence.empty());
  CCA_CHECK(decoded.independence_domains.empty());
  CCA_CHECK(decoded.manifest_declarations.empty());
  CCA_CHECK(decoded.equipment_classes.empty());

  CCA_ASSIGN(reencoded, encode_canonical(decoded));
  CCA_CHECK(reencoded == bytes);

  // The empty input is still a complete, digestible record.
  CCA_ASSIGN(digest, canonical_digest(empty));
  CCA_CHECK_EQ(digest.to_hex(), Digest::of_bytes(bytes.data(), bytes.size()).to_hex());
}

CCA_TEST(canonical_rich_input_round_trips_byte_for_byte) {
  const AccountingInput input = rich_input();
  CCA_ASSIGN(bytes, encode_canonical(input));
  CCA_CHECK(bytes.size() > 512U);
  CCA_CHECK(std::string_view(reinterpret_cast<const char*>(bytes.data()), 8U) ==
            kCanonicalMagic);
  // The declared payload length is exactly the bytes after the length field.
  CCA_CHECK_EQ(read_u64(bytes, 16U), static_cast<std::uint64_t>(bytes.size() - kHeaderBytes));
  CCA_CHECK_EQ(read_u32(bytes, 8U), kCanonicalFormatVersion);
  CCA_CHECK_EQ(read_u32(bytes, 12U), 0U);

  CCA_ASSIGN(decoded, decode_canonical(bytes));
  CCA_CHECK_EQ(decoded.scopes.size(), input.scopes.size());
  CCA_CHECK_EQ(decoded.contributions.size(), input.contributions.size());
  CCA_CHECK_EQ(decoded.evidence.size(), input.evidence.size());
  CCA_CHECK_EQ(decoded.groups.size(), input.groups.size());
  CCA_CHECK_EQ(decoded.independence_domains.size(), input.independence_domains.size());
  CCA_CHECK_EQ(decoded.manifest_declarations.size(), input.manifest_declarations.size());
  CCA_CHECK_EQ(decoded.equipment_classes.size(), input.equipment_classes.size());

  // Records come out in strictly ascending identity order.
  for (std::size_t index = 1; index < decoded.contributions.size(); ++index) {
    CCA_CHECK(decoded.contributions[index - 1U].id < decoded.contributions[index].id);
  }
  for (std::size_t index = 1; index < decoded.scopes.size(); ++index) {
    CCA_CHECK(decoded.scopes[index - 1U].id < decoded.scopes[index].id);
  }

  // encode(decode(bytes)) == bytes.
  CCA_ASSIGN(reencoded, encode_canonical(decoded));
  CCA_CHECK(reencoded == bytes);

  const Bytes& stable = reencoded;
  CCA_ASSIGN(again, decode_canonical(stable));
  CCA_ASSIGN(reencoded_again, encode_canonical(again));
  CCA_CHECK(reencoded_again == stable);

  CCA_ASSIGN(digest, canonical_digest(input));
  CCA_CHECK_EQ(digest.to_hex(), Digest::of_bytes(bytes.data(), bytes.size()).to_hex());
  CCA_ASSIGN(decoded_digest, canonical_digest(decoded));
  CCA_CHECK_EQ(decoded_digest.to_hex(), digest.to_hex());
}

CCA_TEST(canonical_is_independent_of_record_vector_order) {
  const AccountingInput input = rich_input();
  CCA_ASSIGN(expected, encode_canonical(input));
  CCA_ASSIGN(expected_digest, canonical_digest(input));
  CCA_CHECK(!expected.empty());

  constexpr std::uint64_t kSeed = 0x5EED1234C0FFEEULL;
  note("permutation seed " + std::to_string(kSeed));
  SeededRandom random(kSeed);
  constexpr int kPermutations = 32;
  for (int iteration = 0; iteration < kPermutations; ++iteration) {
    AccountingInput shuffled = input;
    shuffle_with(shuffled.scopes, random);
    shuffle_with(shuffled.contributions, random);
    shuffle_with(shuffled.groups, random);
    shuffle_with(shuffled.evidence, random);
    shuffle_with(shuffled.independence_domains, random);
    shuffle_with(shuffled.manifest_declarations, random);
    shuffle_with(shuffled.equipment_classes, random);
    for (Contribution& contribution : shuffled.contributions) {
      shuffle_with(contribution.derates, random);
      shuffle_with(contribution.sharing.shares, random);
    }
    CCA_ASSIGN(bytes, encode_canonical(shuffled));
    if (bytes != expected) {
      CCA_FAIL("permutation " + std::to_string(iteration) + " with seed " +
               std::to_string(kSeed) + " encoded to different bytes");
      continue;
    }
    CCA_ASSIGN(digest, canonical_digest(shuffled));
    CCA_CHECK_EQ(digest.to_hex(), expected_digest.to_hex());
  }
}

// ---------------------------------------------------------------------------
// Header refusals
// ---------------------------------------------------------------------------

CCA_TEST(canonical_refuses_a_wrong_magic) {
  CCA_ASSIGN(bytes, encode_canonical(rich_input()));
  for (std::size_t index = 0; index < 8U; ++index) {
    Bytes mutated = bytes;
    mutated[index] = static_cast<std::uint8_t>(mutated[index] ^ 0x20U);
    CCA_CHECK_CODE(decode_canonical(mutated), ErrorCode::MalformedRecord);
  }
  Bytes truncated_magic = bytes;
  truncated_magic[7] = 0U;
  CCA_CHECK_CODE(decode_canonical(truncated_magic), ErrorCode::MalformedRecord);
}

CCA_TEST(canonical_refuses_an_unsupported_version) {
  CCA_ASSIGN(bytes, encode_canonical(rich_input()));
  for (const std::uint32_t version : {0U, 2U, 0xFFFFFFFFU}) {
    CCA_CHECK_CODE(decode_canonical(with_u32(bytes, 8U, version)),
                   ErrorCode::UnsupportedFormatVersion);
  }
  CCA_CHECK_CODE(decode_canonical(with_u32(bytes, 8U, kCanonicalFormatVersion + 1U)),
                 ErrorCode::UnsupportedFormatVersion);
}

CCA_TEST(canonical_refuses_a_non_zero_reserved_field) {
  CCA_ASSIGN(bytes, encode_canonical(rich_input()));
  for (const std::uint32_t value : {1U, 0x0000FF00U, 0xFFFFFFFFU}) {
    CCA_CHECK_CODE(decode_canonical(with_u32(bytes, 12U, value)),
                   ErrorCode::ReservedFieldNotZero);
  }
}

CCA_TEST(canonical_refuses_a_wrong_declared_length) {
  CCA_ASSIGN(bytes, encode_canonical(rich_input()));
  const std::uint64_t declared = read_u64(bytes, 16U);
  CCA_CHECK_EQ(declared, static_cast<std::uint64_t>(bytes.size() - kHeaderBytes));
  CCA_CHECK_CODE(decode_canonical(with_u64(bytes, 16U, declared + 1U)),
                 ErrorCode::MalformedRecord);
  CCA_CHECK_CODE(decode_canonical(with_u64(bytes, 16U, declared - 1U)),
                 ErrorCode::MalformedRecord);
  CCA_CHECK_CODE(decode_canonical(with_u64(bytes, 16U, 0U)), ErrorCode::MalformedRecord);
  CCA_CHECK_CODE(decode_canonical(with_u64(bytes, 16U, 0xFFFFFFFFFFFFFFFFULL)),
                 ErrorCode::MalformedRecord);
}

// ---------------------------------------------------------------------------
// Truncation and trailing bytes
// ---------------------------------------------------------------------------

CCA_TEST(canonical_refuses_truncation) {
  CCA_ASSIGN(bytes, encode_canonical(rich_input()));

  // Cut before the header is complete: the declared length cannot even be read.
  for (const std::size_t size : {std::size_t{0}, std::size_t{1}, std::size_t{4},
                                 std::size_t{7}, std::size_t{8}, std::size_t{11},
                                 std::size_t{12}, std::size_t{16}, std::size_t{19},
                                 std::size_t{23}}) {
    const Bytes cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
    CCA_CHECK_CODE(decode_canonical(cut), ErrorCode::TruncatedInput);
  }

  // Cut inside the payload, with the declared length repaired to match, so the
  // length fence is not what refuses the input.
  const std::size_t cuts[] = {kHeaderBytes,        kHeaderBytes + 8U,
                              kHeaderBytes + 40U,  kHeaderBytes + 41U,
                              kHeaderBytes + 44U,  kHeaderBytes + 70U,
                              kHeaderBytes + 120U, bytes.size() - 33U,
                              bytes.size() - 9U,   bytes.size() - 1U};
  for (const std::size_t size : cuts) {
    Bytes cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
    cut = with_u64(cut, 16U, static_cast<std::uint64_t>(size - kHeaderBytes));
    CCA_CHECK_CODE(decode_canonical(cut), ErrorCode::TruncatedInput);
  }

  // A cut inside the policy identifier string: the string claims more bytes than
  // remain.
  Bytes inside_string(bytes.begin(),
                      bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes + 44U));
  inside_string = with_u64(inside_string, 16U, 44U);
  CCA_CHECK_CODE(decode_canonical(inside_string), ErrorCode::TruncatedInput);
}

CCA_TEST(canonical_refuses_trailing_bytes) {
  CCA_ASSIGN(bytes, encode_canonical(rich_input()));
  for (const std::size_t extra : {std::size_t{1}, std::size_t{4}, std::size_t{64}}) {
    Bytes appended = bytes;
    appended.insert(appended.end(), extra, 0xA5U);
    appended = with_u64(appended, 16U,
                        static_cast<std::uint64_t>(bytes.size() - kHeaderBytes + extra));
    CCA_CHECK_CODE(decode_canonical(appended), ErrorCode::TrailingBytes);
  }

  // Appending without repairing the declared length is caught even earlier.
  Bytes unpatched = bytes;
  unpatched.push_back(0U);
  CCA_CHECK_CODE(decode_canonical(unpatched), ErrorCode::MalformedRecord);
}

// ---------------------------------------------------------------------------
// Field-level refusals on a payload whose layout is fully known
// ---------------------------------------------------------------------------

CCA_TEST(canonical_minimal_layout_is_exact) {
  CCA_ASSIGN(bytes, encode_canonical(two_class_input()));
  const std::size_t expected = kHeaderBytes + kGenerationsBytes +
                              policy_bytes(two_class_input().policy) +
                              4U /* equipment class count */ + 2U * kClassRecordBytes +
                              6U * 4U /* the six other record counts */ + kTrailerBytes;
  CCA_CHECK_EQ(bytes.size(), expected);
}

CCA_TEST(canonical_refuses_an_impossible_enum_value) {
  const AccountingInput base = two_class_input();
  AccountingInput other = base;
  other.equipment_classes[0].kind = EquipmentClassKind::DryCooler;
  CCA_ASSIGN(bytes, encode_canonical(base));
  CCA_ASSIGN(other_bytes, encode_canonical(other));
  CCA_CHECK_EQ(bytes.size(), other_bytes.size());
  std::size_t differences = 0;
  std::size_t last_difference = 0;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (bytes[index] != other_bytes[index]) {
      differences += 1;
      last_difference = index;
    }
  }
  // Only the enumerated field differs, so every differing byte belongs to it.
  CCA_CHECK(differences > 0U && differences <= 4U);

  Bytes mutated = bytes;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (bytes[index] != other_bytes[index]) {
      mutated[index] = 0xFFU;
    }
  }
  CCA_CHECK(mutated[last_difference] != bytes[last_difference]);
  CCA_CHECK_CODE(decode_canonical(mutated), ErrorCode::ImpossibleEnumValue);

  // The same mutation applied to the medium field of the same record.
  AccountingInput medium_other = base;
  medium_other.equipment_classes[0].medium = Medium::Liquid;
  CCA_ASSIGN(medium_bytes, encode_canonical(medium_other));
  Bytes medium_mutated = bytes;
  bool changed = false;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (bytes[index] != medium_bytes[index]) {
      medium_mutated[index] = 0xFFU;
      changed = true;
    }
  }
  CCA_CHECK(changed);
  CCA_CHECK_CODE(decode_canonical(medium_mutated), ErrorCode::ImpossibleEnumValue);
}

CCA_TEST(canonical_refuses_an_oversized_field_and_count) {
  CCA_ASSIGN(bytes, encode_canonical(two_class_input()));
  const std::size_t alpha = find_record(bytes, "class.alpha");
  CCA_CHECK(alpha > 4U && alpha < bytes.size());

  // A record count above the bound is refused before any allocation.
  CCA_CHECK_CODE(decode_canonical(with_u32(bytes, alpha - 4U, 0xFFFFFFFFU)),
                 ErrorCode::LimitExceeded);
  CCA_CHECK_CODE(decode_canonical(with_u32(bytes, alpha - 4U, 65536U)),
                 ErrorCode::LimitExceeded);

  // A length prefix above the field bound is refused before the bytes are used.
  CCA_CHECK_CODE(decode_canonical(with_u32(bytes, alpha, 65U)), ErrorCode::ItemTooLarge);
  CCA_CHECK_CODE(decode_canonical(with_u32(bytes, alpha, 0xFFFFFFFFU)),
                 ErrorCode::ItemTooLarge);

  // The policy identifier is the first length-prefixed field of the payload, so
  // the same bound applies there.
  CCA_CHECK_CODE(decode_canonical(with_u32(bytes, kHeaderBytes + kGenerationsBytes, 65U)),
                 ErrorCode::ItemTooLarge);

  // A record count above the caller's lowered limit is refused too.
  Limits lowered;
  lowered.max_scopes = 1U;
  CCA_CHECK_CODE(decode_canonical(bytes, lowered), ErrorCode::LimitExceeded);
}

CCA_TEST(canonical_refuses_out_of_order_and_duplicate_records) {
  CCA_ASSIGN(bytes, encode_canonical(two_class_input()));
  const std::size_t alpha = find_record(bytes, "class.alpha");
  const std::size_t bravo = find_record(bytes, "class.bravo");
  CCA_CHECK(bravo > alpha);
  const std::size_t record_bytes = bravo - alpha;
  CCA_CHECK_EQ(record_bytes, kClassRecordBytes);
  CCA_CHECK_EQ(read_u32(bytes, alpha), 11U);  // "class.alpha" is length prefixed

  // Swap the two records: the payload is complete but no longer ascending.
  Bytes swapped = bytes;
  for (std::size_t index = 0; index < record_bytes; ++index) {
    swapped[alpha + index] = bytes[bravo + index];
    swapped[bravo + index] = bytes[alpha + index];
  }
  CCA_CHECK_CODE(decode_canonical(swapped), ErrorCode::RecordOrderViolation);

  // Duplicate the first record over the second: the order is not strict.
  Bytes duplicated = bytes;
  for (std::size_t index = 0; index < record_bytes; ++index) {
    duplicated[bravo + index] = bytes[alpha + index];
  }
  CCA_CHECK_CODE(decode_canonical(duplicated), ErrorCode::RecordOrderViolation);
}

CCA_TEST(canonical_refuses_a_contribution_order_violation) {
  // Two contributions of identical length, so their records can be exchanged
  // byte for byte.
  AccountingInput input = cca_test::make_facility(cca_test::FacilitySpec{});
  input.contributions[0].id = id_of<ContributionId>("contribution.alpha");
  input.contributions[1].id = id_of<ContributionId>("contribution.bravo");
  input.contributions.resize(2U);
  CCA_ASSIGN(bytes, encode_canonical(input));

  const std::size_t alpha = find_record(bytes, "contribution.alpha");
  const std::size_t bravo = find_record(bytes, "contribution.bravo");
  CCA_CHECK(alpha > 4U && bravo > alpha);
  const std::size_t record_bytes = bravo - alpha;

  Bytes swapped = bytes;
  for (std::size_t index = 0; index < record_bytes; ++index) {
    swapped[alpha + index] = bytes[bravo + index];
    swapped[bravo + index] = bytes[alpha + index];
  }
  CCA_CHECK_CODE(decode_canonical(swapped), ErrorCode::RecordOrderViolation);
}

CCA_TEST(canonical_refuses_an_out_of_order_derate) {
  AccountingInput input;
  input.policy.id = id_of<PolicyId>("p");
  Contribution contribution = cca_test::make_contribution(
      "contribution.alpha", "loop.a.0", "class.crah", "equipment.alpha", 100'000,
      GenerationBundle::initial());
  contribution.derates.push_back(
      DerateFactor(id_of<DerateId>("derate.alpha"), Ratio::of_ppm(900'000).value()));
  contribution.derates.push_back(
      DerateFactor(id_of<DerateId>("derate.bravo"), Ratio::of_ppm(800'000).value()));
  input.contributions.push_back(contribution);
  CCA_ASSIGN(bytes, encode_canonical(input));

  const std::size_t alpha = find_record(bytes, "derate.alpha");
  const std::size_t bravo = find_record(bytes, "derate.bravo");
  CCA_CHECK(bravo > alpha);
  const std::size_t record_bytes = bravo - alpha;
  Bytes swapped = bytes;
  for (std::size_t index = 0; index < record_bytes; ++index) {
    swapped[alpha + index] = bytes[bravo + index];
    swapped[bravo + index] = bytes[alpha + index];
  }
  CCA_CHECK_CODE(decode_canonical(swapped), ErrorCode::RecordOrderViolation);
}

// ---------------------------------------------------------------------------
// Size bounds
// ---------------------------------------------------------------------------

CCA_TEST(canonical_refuses_a_payload_beyond_the_bound) {
  // The bound is checked before the data is touched: a one-byte buffer with a
  // size beyond the bound is refused rather than read.
  const std::uint8_t byte = 0U;
  CCA_CHECK_CODE(decode_canonical(&byte, kMaxCanonicalPayloadBytes + 1U),
                 ErrorCode::ItemTooLarge);
  CCA_CHECK_CODE(decode_canonical(nullptr, kMaxCanonicalPayloadBytes + 1U),
                 ErrorCode::ItemTooLarge);

  // One byte inside the bound is read, and fails on the magic instead.
  CCA_CHECK_CODE(decode_canonical(&byte, 1U), ErrorCode::TruncatedInput);
}

CCA_TEST(canonical_refuses_input_beyond_the_encoder_limits) {
  const AccountingInput input = rich_input();
  Limits lowered;
  lowered.max_scopes = 1U;
  CCA_CHECK_CODE(encode_canonical(input, lowered), ErrorCode::LimitExceeded);

  Limits tiny;
  tiny.max_contributions = 1U;
  CCA_CHECK_CODE(encode_canonical(input, tiny), ErrorCode::LimitExceeded);

  Limits zero;
  zero.max_scopes = 0U;
  CCA_CHECK_CODE(encode_canonical(input, zero), ErrorCode::InvalidArgument);
  CCA_CHECK_CODE(decode_canonical(Bytes{}, zero), ErrorCode::InvalidArgument);

  Limits raised;
  raised.max_scopes = kMaxScopes + 1U;
  CCA_CHECK_CODE(encode_canonical(input, raised), ErrorCode::LimitExceeded);
}
