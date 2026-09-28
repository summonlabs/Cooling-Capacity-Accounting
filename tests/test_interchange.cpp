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

// Interchange text: a byte-stable round trip and the exact refusal of every
// malformed line the grammar defines.

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
using cca_test::fixture_ago;
using cca_test::fixture_now;
using cca_test::id_of;

namespace {

BoundedText text_of(std::string_view value) {
  return BoundedText::from_validated(std::string(value));
}

std::string trim_end(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  return text;
}

/// The first line that begins with the keyword, or npos.
[[nodiscard]] std::size_t find_line(const std::string& text, std::string_view keyword) {
  const std::string needle(keyword);
  std::size_t at = 0;
  while (at <= text.size()) {
    const std::size_t line_end = text.find('\n', at);
    const std::size_t stop = line_end == std::string::npos ? text.size() : line_end;
    if (stop >= at + needle.size() && text.compare(at, needle.size(), needle) == 0 &&
        (stop == at + needle.size() || text[at + needle.size()] == ' ' ||
         needle.back() != ' ')) {
      return at;
    }
    if (line_end == std::string::npos) {
      break;
    }
    at = line_end + 1U;
  }
  return std::string::npos;
}

/// [start, end) of the first line that begins with keyword, newline included.
[[nodiscard]] bool line_range(const std::string& text, std::string_view keyword,
                              std::size_t& start, std::size_t& end) {
  start = find_line(text, keyword);
  if (start == std::string::npos) {
    return false;
  }
  const std::size_t newline = text.find('\n', start);
  end = newline == std::string::npos ? text.size() : newline + 1U;
  return true;
}

[[nodiscard]] std::string replace_once(std::string text, std::string_view from,
                                       std::string_view to) {
  const std::size_t at = text.find(from);
  CCA_CHECK(at != std::string::npos);
  if (at == std::string::npos) {
    return text;
  }
  text.replace(at, from.size(), to);
  return text;
}

/// Replaces the value of the first " key=" occurrence.
[[nodiscard]] std::string replace_field_value(std::string text, std::string_view key,
                                              std::string_view value) {
  const std::string needle = " " + std::string(key) + "=";
  const std::size_t at = text.find(needle);
  CCA_CHECK(at != std::string::npos);
  if (at == std::string::npos) {
    return text;
  }
  const std::size_t value_start = at + needle.size();
  const std::size_t value_end = text.find_first_of(" \n", value_start);
  const std::size_t stop = value_end == std::string::npos ? text.size() : value_end;
  text.replace(value_start, stop - value_start, value);
  return text;
}

/// Inserts a whole line immediately after the line that begins with keyword.
[[nodiscard]] std::string insert_after_line(std::string text, std::string_view keyword,
                                            std::string_view line) {
  std::size_t start = 0;
  std::size_t end = 0;
  CCA_CHECK(line_range(text, keyword, start, end));
  if (!line_range(text, keyword, start, end)) {
    return text;
  }
  text.insert(end, std::string(line) + "\n");
  return text;
}

/// Moves the line that begins with keyword to immediately after the line that
/// begins with after.
[[nodiscard]] std::string move_line_after(std::string text, std::string_view keyword,
                                          std::string_view after) {
  std::size_t start = 0;
  std::size_t end = 0;
  CCA_CHECK(line_range(text, keyword, start, end));
  if (!line_range(text, keyword, start, end)) {
    return text;
  }
  const std::string line = text.substr(start, end - start);
  text.erase(start, end - start);
  std::size_t after_start = 0;
  std::size_t after_end = 0;
  CCA_CHECK(line_range(text, after, after_start, after_end));
  if (!line_range(text, after, after_start, after_end)) {
    return text;
  }
  text.insert(after_end, line);
  return text;
}

constexpr std::string_view kObservedAt = "2026-01-01T00:00:00.000Z";

}  // namespace

namespace {

/// A complete input that exercises every record the text form defines and stays
/// inside the field set the writer can express without loss.
[[nodiscard]] AccountingInput interchange_input() {
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

  const EvidenceBinding binding{input.generations.epoch, input.generations.topology,
                                input.generations.policy, input.generations.evidence};

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
  nameplate.binding = binding;
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
  silent.binding = binding;
  silent.medium = Medium::Liquid;
  silent.declared_value =
      Measure<ThermalPower>::unknown(MeasureReason::SensorAbsent, BoundedText());
  input.evidence.push_back(silent);

  EvidenceRecord unsupported;
  unsupported.id = id_of<EvidenceId>("evidence.gamma");
  unsupported.kind = EvidenceKind::OperatorDeclaration;
  unsupported.source_kind = EvidenceSourceKind::Operator;
  unsupported.source = id_of<EvidenceSourceId>("source.operator");
  unsupported.observed_at = fixture_ago(600'000);
  unsupported.recorded_at = fixture_ago(500'000);
  unsupported.sequence = ObservationSequence::of(3).value();
  unsupported.binding = binding;
  unsupported.content_digest = Digest::of_text("gamma");
  unsupported.medium = Medium::Air;
  unsupported.declared_value = Measure<ThermalPower>::unsupported(
      MeasureReason::NotCommissioned, BoundedText());
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
  input.scopes.push_back(zone);

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
  group.binding = binding;
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
  degraded.derates.push_back(DerateFactor(id_of<DerateId>("derate.bravo.factor"),
                                          Ratio::of_ppm(850'000).value()));
  degraded.sharing.kind = Sharing::Kind::Apportioned;
  degraded.sharing.shares.push_back(
      ApportionmentShare{id_of<ScopeId>("zone.a"), Ratio::of_ppm(250'000).value()});
  degraded.sharing.shares.push_back(
      ApportionmentShare{id_of<ScopeId>("site.alpha"), Ratio::of_ppm(100'000).value()});
  degraded.evidence.push_back(id_of<EvidenceId>("evidence.beta"));
  degraded.observed_at = fixture_ago(1'000'000);
  input.contributions.push_back(degraded);

  Contribution standby = cca_test::make_contribution(
      "contribution.charlie", "loop.a.0", "class.chiller", "equipment.charlie", 10'000,
      input.generations);
  standby.installed =
      Measure<ThermalPower>::unknown(MeasureReason::InventoryUnknown, BoundedText());
  standby.service = ServiceState::OutOfService;
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
  declaration.binding = binding;
  input.manifest_declarations.push_back(declaration);

  ManifestDeclaration second;
  second.id = id_of<ManifestId>("manifest.bravo");
  second.scope = id_of<ScopeId>("zone.a");
  second.medium = Medium::Air;
  second.declared_installed = ThermalPower::of_milliwatts(750'000);
  second.evidence = id_of<EvidenceId>("evidence.gamma");
  second.observed_at = fixture_ago(2'000'000);
  second.binding = binding;
  input.manifest_declarations.push_back(second);

  return input;
}

/// A small but complete document, used as the base of the mutation tests.
[[nodiscard]] AccountingInput minimal_input() {
  AccountingInput input;
  input.policy.id = id_of<PolicyId>("p");
  input.policy.effective_from = fixture_ago(1'000);

  EquipmentClass klass;
  klass.id = id_of<EquipmentClassId>("class.alpha");
  klass.kind = EquipmentClassKind::ComputerRoomAirHandler;
  klass.medium = Medium::Air;
  klass.label = text_of("alpha unit");
  klass.contributes_to_installed = true;
  input.equipment_classes.push_back(klass);

  AccountingScope site;
  site.id = id_of<ScopeId>("site.alpha");
  site.kind = ScopeKind::Site;
  site.medium = Medium::Air;
  input.scopes.push_back(site);

  input.evidence.push_back(cca_test::make_evidence("evidence.alpha", EvidenceKind::Nameplate,
                                                   fixture_ago(2'000), input.generations));

  input.contributions.push_back(cca_test::make_contribution(
      "contribution.alpha", "site.alpha", "class.alpha", "equipment.alpha", 100'000,
      input.generations));

  ManifestDeclaration declaration;
  declaration.id = id_of<ManifestId>("manifest.alpha");
  declaration.scope = id_of<ScopeId>("site.alpha");
  declaration.medium = Medium::Air;
  declaration.declared_installed = ThermalPower::of_milliwatts(100'000);
  declaration.evidence = id_of<EvidenceId>("evidence.alpha");
  declaration.observed_at = fixture_ago(3'000);
  input.manifest_declarations.push_back(declaration);
  return input;
}

/// The document text of one input, checked to be well formed first.
[[nodiscard]] std::string text_of_input(const AccountingInput& input) {
  const Result<std::string> written = write_interchange(input);
  CCA_CHECK(written.ok());
  if (!written.ok()) {
    return std::string();
  }
  CCA_CHECK(parse_interchange(written.value()).ok());
  return written.value();
}

/// The canonical bytes of an input, so two inputs can be compared without an
/// operator on the whole structure.
[[nodiscard]] std::string bytes_of(const AccountingInput& input) {
  const Result<std::vector<std::uint8_t>> encoded = encode_canonical(input);
  CCA_CHECK(encoded.ok());
  if (!encoded.ok()) {
    return std::string();
  }
  return std::string(reinterpret_cast<const char*>(encoded.value().data()),
                     encoded.value().size());
}

}  // namespace

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

CCA_TEST(interchange_round_trips_a_rich_document_byte_for_byte) {
  const AccountingInput input = interchange_input();
  CCA_ASSIGN(text, write_interchange(input));
  CCA_CHECK(text.rfind("CCA-INTERCHANGE 1\n", 0) == 0);
  CCA_CHECK(text.size() > 1024U);
  CCA_CHECK_EQ(text.substr(text.size() - 4U), std::string("END\n"));

  CCA_ASSIGN(parsed, parse_interchange(text));
  CCA_CHECK_EQ(parsed.scopes.size(), input.scopes.size());
  CCA_CHECK_EQ(parsed.contributions.size(), input.contributions.size());
  CCA_CHECK_EQ(parsed.evidence.size(), input.evidence.size());
  CCA_CHECK_EQ(parsed.groups.size(), input.groups.size());
  CCA_CHECK_EQ(parsed.independence_domains.size(), input.independence_domains.size());
  CCA_CHECK_EQ(parsed.manifest_declarations.size(), input.manifest_declarations.size());
  CCA_CHECK_EQ(parsed.equipment_classes.size(), input.equipment_classes.size());
  CCA_CHECK_EQ(parsed.generations.revision.value(), input.generations.revision.value());
  CCA_CHECK_EQ(parsed.policy.note.str(), input.policy.note.str());

  // The text round trip preserves the whole logical content: the canonical
  // encoding of the parsed input is the encoding of the original.
  CCA_CHECK_EQ(bytes_of(parsed), bytes_of(input));

  // Re-rendering the parsed document produces exactly the same bytes.
  CCA_ASSIGN(rendered, write_interchange(parsed));
  CCA_CHECK_EQ(rendered, text);

  CCA_ASSIGN(again, parse_interchange(rendered));
  CCA_ASSIGN(rendered_again, write_interchange(again));
  CCA_CHECK_EQ(rendered_again, text);
}

CCA_TEST(interchange_accepts_records_in_any_order_within_a_rank) {
  const AccountingInput input = interchange_input();
  const std::string text = text_of_input(input);
  // The two CLASS lines are rank 3: exchanging them keeps the document ordered.
  const std::string swapped =
      move_line_after(text, "CLASS id=class.chiller", "CLASS id=class.crah");
  CCA_CHECK(swapped != text);
  CCA_ASSIGN(parsed, parse_interchange(swapped));
  CCA_CHECK_EQ(bytes_of(parsed), bytes_of(input));
  // The canonical rendering is sorted again, so it is unchanged.
  CCA_ASSIGN(rendered, write_interchange(parsed));
  CCA_CHECK_EQ(rendered, text);
}

CCA_TEST(interchange_quoting_round_trips_space_quote_and_backslash) {
  AccountingInput input = minimal_input();
  const std::string tricky = "a \"quoted\" label with \\ backslash and spaces";
  const std::string tricky_notes = "note with \"quotes\" and \\slashes\\";
  const std::string tricky_field =
      "label=\"a \\\"quoted\\\" label with \\\\ backslash and spaces\"";
  const std::string notes_field = "notes=\"note with \\\"quotes\\\" and \\\\slashes\\\\\"";
  input.equipment_classes[0].label = text_of(tricky);
  input.evidence[0].notes = text_of(tricky_notes);

  CCA_ASSIGN(text, write_interchange(input));
  CCA_CHECK(text.find(tricky_field) != std::string::npos);
  CCA_CHECK(text.find(notes_field) != std::string::npos);

  CCA_ASSIGN(parsed, parse_interchange(text));
  CCA_CHECK_EQ(parsed.equipment_classes[0].label.str(), tricky);
  CCA_CHECK_EQ(parsed.evidence[0].notes.str(), tricky_notes);
  CCA_ASSIGN(rendered, write_interchange(parsed));
  CCA_CHECK_EQ(rendered, text);
  CCA_CHECK_EQ(bytes_of(parsed), bytes_of(input));

  // An empty quoted value is accepted and means an empty text.
  CCA_ASSIGN(empty_parsed, parse_interchange(replace_once(text, tricky_field, "label=\"\"")));
  CCA_CHECK(empty_parsed.equipment_classes[0].label.empty());
}

// ---------------------------------------------------------------------------
// Structural refusals
// ---------------------------------------------------------------------------

CCA_TEST(interchange_refuses_a_missing_header) {
  const std::string text = text_of_input(minimal_input());
  CCA_CHECK(!text.empty());

  // No text at all: there is no header line to find.
  CCA_CHECK_CODE(parse_interchange(std::string_view()), ErrorCode::MissingRequiredField);

  // The document starts with a record instead of the header.
  const std::size_t first_newline = text.find('\n');
  CCA_CHECK(first_newline != std::string::npos);
  CCA_CHECK_CODE(parse_interchange(text.substr(first_newline + 1U)),
                 ErrorCode::RecordOrderViolation);

  // A header that is not the first line is refused.
  CCA_CHECK_CODE(parse_interchange(text.substr(0, first_newline + 1U) + text),
                 ErrorCode::RecordOrderViolation);

  // A second header after END is trailing text.
  CCA_CHECK_CODE(parse_interchange(text + text), ErrorCode::TrailingBytes);
  CCA_CHECK_CODE(
      parse_interchange(replace_once(text, "POLICY id=", "CCA-INTERCHANGE 1\nPOLICY id=")),
      ErrorCode::RecordOrderViolation);
}

CCA_TEST(interchange_refuses_a_wrong_version) {
  const std::string text = text_of_input(minimal_input());
  CCA_CHECK_CODE(parse_interchange(replace_once(text, "CCA-INTERCHANGE 1", "CCA-INTERCHANGE 0")),
                 ErrorCode::UnsupportedFormatVersion);
  CCA_CHECK_CODE(parse_interchange(replace_once(text, "CCA-INTERCHANGE 1", "CCA-INTERCHANGE 2")),
                 ErrorCode::UnsupportedFormatVersion);
  CCA_CHECK_CODE(
      parse_interchange(replace_once(text, "CCA-INTERCHANGE 1", "CCA-INTERCHANGE 4294967295")),
      ErrorCode::UnsupportedFormatVersion);

  // The header line is exactly the keyword, one space and a decimal.
  CCA_CHECK_CODE(parse_interchange(replace_once(text, "CCA-INTERCHANGE 1", "CCA-INTERCHANGE")),
                 ErrorCode::MalformedRecord);
  CCA_CHECK_CODE(parse_interchange(replace_once(text, "CCA-INTERCHANGE 1", "CCA-INTERCHANGE  1")),
                 ErrorCode::MalformedRecord);
  CCA_CHECK_CODE(parse_interchange(replace_once(text, "CCA-INTERCHANGE 1", "CCA-INTERCHANGE x")),
                 ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_interchange(replace_once(text, "CCA-INTERCHANGE 1", "CCA-INTERCHANGE 01")),
                 ErrorCode::InvalidNumber);
}

CCA_TEST(interchange_refuses_an_undefined_keyword) {
  const std::string text = text_of_input(minimal_input());
  // A record keyword the format does not define.
  CCA_CHECK_CODE(parse_interchange(insert_after_line(text, "POLICY", "FLURB id=x")),
                 ErrorCode::UnknownKeyword);
  // A field key the record does not define.
  CCA_CHECK_CODE(parse_interchange(replace_once(text, "revision=0", "revision=0 bogus=1")),
                 ErrorCode::UnknownKeyword);
  // Keyword matching is case sensitive.
  CCA_CHECK_CODE(parse_interchange(replace_once(text, "CONTRIBUTION id=", "Contribution id=")),
                 ErrorCode::UnknownKeyword);
}

CCA_TEST(interchange_refuses_a_record_out_of_order) {
  const std::string text = text_of_input(interchange_input());
  // A CLASS moved after the MANIFEST records.
  CCA_CHECK_CODE(parse_interchange(move_line_after(text, "CLASS id=class.crah", "MANIFEST")),
                 ErrorCode::RecordOrderViolation);
  // A POLICY moved after the contributions.
  CCA_CHECK_CODE(parse_interchange(move_line_after(text, "POLICY", "MANIFEST")),
                 ErrorCode::RecordOrderViolation);
  // GENERATIONS moved to the end.
  CCA_CHECK_CODE(parse_interchange(move_line_after(text, "GENERATIONS", "MANIFEST")),
                 ErrorCode::RecordOrderViolation);
  // A SCOPE moved before GENERATIONS.
  const std::string scope_first =
      move_line_after(text, "SCOPE id=site.alpha", "CCA-INTERCHANGE 1");
  CCA_CHECK(scope_first != text);
  CCA_CHECK_CODE(parse_interchange(scope_first), ErrorCode::RecordOrderViolation);
}

CCA_TEST(interchange_refuses_a_duplicate_key) {
  const std::string text = text_of_input(minimal_input());
  CCA_CHECK_CODE(parse_interchange(replace_once(text, " topology=1", " topology=1 topology=1")),
                 ErrorCode::DuplicateField);
  CCA_CHECK_CODE(parse_interchange(replace_once(text, " medium=Air", " medium=Air medium=Air")),
                 ErrorCode::DuplicateField);
  // GENERATIONS appears more than once.
  CCA_CHECK_CODE(
      parse_interchange(insert_after_line(text, "GENERATIONS",
                                          "GENERATIONS epoch=1 topology=1 policy=1 "
                                          "evidence=1 revision=0")),
      ErrorCode::DuplicateField);
  // POLICY appears more than once.
  const std::string duplicate_policy =
      "POLICY id=q generation=1 epoch=1 effective_from=" + std::string(kObservedAt) +
      " max_evidence_age_ms=0 minimum_coverage_ppm=1000000 residual_tolerance_ppm=0 "
      "max_derate_factors=3 retention_generations=64 "
      "out_of_service_requires_evidence=true classification_required=true "
      "installed_evidence_required=true derate_evidence_required=true";
  CCA_CHECK_CODE(parse_interchange(insert_after_line(text, "POLICY", duplicate_policy)),
                 ErrorCode::DuplicateField);
}

CCA_TEST(interchange_refuses_a_missing_required_key) {
  const std::string text = text_of_input(minimal_input());
  CCA_CHECK_CODE(parse_interchange(replace_once(text, " revision=0", "")),
                 ErrorCode::MissingRequiredField);
  CCA_CHECK_CODE(parse_interchange(replace_once(text, " epoch=1", "")),
                 ErrorCode::MissingRequiredField);
  // A record that does not state its quantity at all.
  CCA_CHECK_CODE(parse_interchange(replace_once(text, " installed_mw=100000", "")),
                 ErrorCode::MissingRequiredField);
  // A CLASS without its kind.
  CCA_CHECK_CODE(parse_interchange(replace_once(text, " kind=ComputerRoomAirHandler", "")),
                 ErrorCode::MissingRequiredField);
  // A missing END is a truncated document, not a missing field.
  const std::string trimmed = trim_end(text);
  CCA_CHECK_CODE(parse_interchange(trimmed.substr(0, trimmed.rfind('\n'))),
                 ErrorCode::TruncatedInput);
}

CCA_TEST(interchange_refuses_an_unterminated_quote) {
  const std::string text = text_of_input(minimal_input());
  CCA_CHECK_CODE(
      parse_interchange(replace_once(text, "label=\"alpha unit\"", "label=\"alpha unit")),
      ErrorCode::MalformedRecord);
  // A quoted value must be followed by a space or the end of the line.
  CCA_CHECK_CODE(
      parse_interchange(replace_once(text, "label=\"alpha unit\"", "label=\"alpha unit\"x")),
      ErrorCode::MalformedRecord);
  // Only quote and backslash may be escaped.
  CCA_CHECK_CODE(
      parse_interchange(replace_once(text, "label=\"alpha unit\"", "label=\"alpha unit\\q\"")),
      ErrorCode::MalformedRecord);
  // A quoted value that ends with a dangling escape character.
  CCA_CHECK_CODE(parse_interchange(replace_once(text, "label=\"alpha unit\"",
                                                "label=\"alpha unit\\")),
                 ErrorCode::MalformedRecord);
}

CCA_TEST(interchange_refuses_an_empty_line) {
  const std::string text = text_of_input(minimal_input());
  CCA_CHECK_CODE(parse_interchange(replace_once(text, "\nEND\n", "\n\nEND\n")),
                 ErrorCode::MalformedRecord);
  CCA_CHECK_CODE(parse_interchange(std::string("\n") + text), ErrorCode::MalformedRecord);
  // An empty line after END is still an empty line, and refused as one.
  CCA_CHECK_CODE(parse_interchange(text + std::string("\n")), ErrorCode::MalformedRecord);
}

CCA_TEST(interchange_refuses_an_over_long_line) {
  const std::string text = text_of_input(minimal_input());
  const std::string long_line(kMaxLineLength + 1U, 'x');
  CCA_CHECK_CODE(parse_interchange(replace_once(text, "END\n", long_line + "\nEND\n")),
                 ErrorCode::ItemTooLarge);
  // Exactly at the bound the length is accepted, and the content is refused:
  // an unknown keyword of that length, reported with a bounded subject.
  const std::string at_bound(kMaxLineLength, 'x');
  const Result<AccountingInput> at_bound_result =
      parse_interchange(replace_once(text, "END\n", at_bound + "\nEND\n"));
  CCA_CHECK(!at_bound_result.ok());
  CCA_CHECK_EQ(at_bound_result.error().code(), ErrorCode::UnknownKeyword);
  CCA_CHECK(at_bound_result.error().subject().size() < at_bound.size());
}

CCA_TEST(interchange_refuses_a_derate_naming_an_unknown_contribution) {
  const std::string text = text_of_input(minimal_input());
  CCA_CHECK_CODE(parse_interchange(insert_after_line(
                     text, "CONTRIBUTION",
                     "DERATE contribution=contribution.nope id=d1 kind=factor ppm=900000")),
                 ErrorCode::UnknownReference);
  // A DERATE with no contribution before it at all.
  CCA_CHECK_CODE(parse_interchange(insert_after_line(
                     text, "SCOPE", "DERATE contribution=contribution.alpha id=d1 "
                                    "kind=factor ppm=900000")),
                 ErrorCode::UnknownReference);
  // A SHARE naming a contribution that has not been read.
  CCA_CHECK_CODE(parse_interchange(insert_after_line(
                     text, "CONTRIBUTION",
                     "SHARE contribution=contribution.nope target=site.alpha ppm=1000")),
                 ErrorCode::UnknownReference);
}

CCA_TEST(interchange_refuses_text_after_end) {
  const std::string text = text_of_input(minimal_input());
  CCA_CHECK_CODE(
      parse_interchange(text + std::string("CLASS id=class.extra kind=Chiller medium=Air\n")),
      ErrorCode::TrailingBytes);
  CCA_CHECK_CODE(parse_interchange(text + std::string("x\n")), ErrorCode::TrailingBytes);
  CCA_CHECK_CODE(parse_interchange(text + std::string("END\n")), ErrorCode::TrailingBytes);
}

CCA_TEST(interchange_refuses_a_missing_end) {
  const std::string text = trim_end(text_of_input(minimal_input()));
  CCA_CHECK_EQ(text.substr(text.size() - 3U), std::string("END"));
  const std::string without_end = text.substr(0, text.rfind('\n') + 1U);
  CCA_CHECK(without_end.find("END") == std::string::npos);
  CCA_CHECK_CODE(parse_interchange(without_end), ErrorCode::TruncatedInput);
}

// ---------------------------------------------------------------------------
// Field-level refusals
// ---------------------------------------------------------------------------

CCA_TEST(interchange_refuses_an_unknown_enum_value) {
  const std::string text = text_of_input(minimal_input());
  CCA_CHECK_CODE(parse_interchange(replace_field_value(text, "medium", "Plasma")),
                 ErrorCode::ImpossibleEnumValue);
  CCA_CHECK_CODE(parse_interchange(replace_field_value(text, "kind", "Teleporter")),
                 ErrorCode::ImpossibleEnumValue);
  CCA_CHECK_CODE(parse_interchange(replace_field_value(text, "classification", "Maybe")),
                 ErrorCode::ImpossibleEnumValue);
  CCA_CHECK_CODE(parse_interchange(replace_field_value(text, "service", "Sometimes")),
                 ErrorCode::ImpossibleEnumValue);
  CCA_CHECK_CODE(parse_interchange(replace_field_value(text, "source_kind", "Oracle")),
                 ErrorCode::ImpossibleEnumValue);
}

CCA_TEST(interchange_refuses_a_bad_boolean) {
  const std::string text = text_of_input(minimal_input());
  CCA_CHECK_CODE(parse_interchange(replace_field_value(text, "contributes", "yes")),
                 ErrorCode::ImpossibleEnumValue);
  CCA_CHECK_CODE(parse_interchange(replace_field_value(text, "contributes", "TRUE")),
                 ErrorCode::ImpossibleEnumValue);
  CCA_CHECK_CODE(parse_interchange(replace_field_value(text, "contributes", "1")),
                 ErrorCode::ImpossibleEnumValue);
  CCA_CHECK_CODE(
      parse_interchange(replace_field_value(text, "installed_evidence_required", "no")),
      ErrorCode::ImpossibleEnumValue);
}

CCA_TEST(interchange_refuses_a_bad_timestamp) {
  const std::string text = text_of_input(minimal_input());
  CCA_CHECK(text.find(kObservedAt) != std::string::npos);
  CCA_CHECK_CODE(parse_interchange(replace_field_value(text, "observed_at", "2026-01-01")),
                 ErrorCode::InvalidTimestampText);
  CCA_CHECK_CODE(parse_interchange(
                     replace_field_value(text, "observed_at", "2026-13-01T00:00:00.000Z")),
                 ErrorCode::InvalidTimestampText);
  CCA_CHECK_CODE(parse_interchange(
                     replace_field_value(text, "observed_at", "2026-02-30T00:00:00.000Z")),
                 ErrorCode::InvalidTimestampText);
  CCA_CHECK_CODE(parse_interchange(
                     replace_field_value(text, "observed_at", "2026-01-01T00:00:00.000+01")),
                 ErrorCode::InvalidTimestampText);
  // A value with a space is not a value at all: the rest is not a key=value pair.
  CCA_CHECK_CODE(parse_interchange(
                     replace_field_value(text, "observed_at", "2026-01-01 00:00:00.000Z")),
                 ErrorCode::MalformedRecord);
}

CCA_TEST(interchange_refuses_a_malformed_decimal) {
  const std::string text = text_of_input(minimal_input());
  CCA_CHECK_CODE(parse_interchange(replace_field_value(text, "revision", "007")),
                 ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_interchange(replace_field_value(text, "revision", "-1")),
                 ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_interchange(replace_field_value(text, "revision", "+1")),
                 ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_interchange(replace_field_value(text, "revision", "")),
                 ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(
      parse_interchange(replace_field_value(text, "minimum_coverage_ppm", "1000001")),
      ErrorCode::OutOfRange);
  CCA_CHECK_CODE(parse_interchange(replace_field_value(text, "epoch", "0")),
                 ErrorCode::OutOfRange);
}

CCA_TEST(interchange_refuses_a_line_that_is_not_a_record) {
  const std::string text = text_of_input(minimal_input());
  CCA_CHECK_CODE(parse_interchange(replace_once(text, "END\n", "END extra\n")),
                 ErrorCode::MalformedRecord);
  // A bare word is not a record keyword this format defines.
  CCA_CHECK_CODE(parse_interchange(insert_after_line(text, "POLICY", "nonsense")),
                 ErrorCode::UnknownKeyword);
  // A line that starts with a field is not a record at all.
  CCA_CHECK_CODE(parse_interchange(insert_after_line(text, "POLICY", "=value")),
                 ErrorCode::MalformedRecord);
  // Two spaces between fields are not a separator.
  CCA_CHECK_CODE(parse_interchange(replace_once(text, "revision=0", "revision=0  bogus=1")),
                 ErrorCode::MalformedRecord);
}

CCA_TEST(interchange_refuses_a_quantity_stated_twice) {
  const std::string text = text_of_input(minimal_input());
  CCA_CHECK_CODE(
      parse_interchange(replace_once(text, "installed_mw=100000",
                                     "installed_mw=100000 installed_unknown=NotMeasured")),
      ErrorCode::MalformedRecord);
}

CCA_TEST(interchange_refuses_an_empty_list_element) {
  AccountingInput input = minimal_input();
  input.scopes[0].classes.push_back(id_of<EquipmentClassId>("class.alpha"));
  const std::string text = text_of_input(input);
  CCA_CHECK(text.find("classes=class.alpha") != std::string::npos);
  CCA_CHECK_CODE(
      parse_interchange(replace_once(text, "classes=class.alpha", "classes=class.alpha,")),
      ErrorCode::MalformedRecord);
  CCA_CHECK_CODE(
      parse_interchange(replace_once(text, "classes=class.alpha", "classes=,class.alpha")),
      ErrorCode::MalformedRecord);
}
