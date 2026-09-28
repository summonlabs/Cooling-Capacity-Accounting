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

#include "cooling_capacity_accounting/interchange.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cooling_capacity_accounting/clock.hpp"
#include "cooling_capacity_accounting/version.hpp"

namespace cooling_capacity_accounting {
namespace {

[[nodiscard]] Error line_error(ErrorCode code, std::size_t line, std::string message) {
  return Error::of(code, std::move(message))
      .with_detail("line " + to_decimal(static_cast<std::uint64_t>(line)));
}

/// One key=value field, with quoting already resolved.
struct Field {
  std::string key;
  std::string value;
};

/// One parsed record.
class Record {
 public:
  Record(std::string keyword, std::size_t line)
      : keyword_(std::move(keyword)), line_(line) {}

  [[nodiscard]] const std::string& keyword() const noexcept { return keyword_; }
  [[nodiscard]] std::size_t line() const noexcept { return line_; }

  [[nodiscard]] Result<void> add(std::string key, std::string value) {
    if (fields_.find(key) != fields_.end()) {
      return line_error(ErrorCode::DuplicateField, line_,
                        "the field appears more than once in this record")
          .with_subject(key);
    }
    fields_.emplace(std::move(key), std::move(value));
    return Ok{};
  }

  [[nodiscard]] bool has(std::string_view key) const {
    return fields_.find(std::string(key)) != fields_.end();
  }

  [[nodiscard]] Result<std::string> require(std::string_view key) const {
    const auto found = fields_.find(std::string(key));
    if (found == fields_.end()) {
      return line_error(ErrorCode::MissingRequiredField, line_,
                        "the record does not carry a required field")
          .with_subject(std::string(key));
    }
    return found->second;
  }

  [[nodiscard]] Result<std::string> optional(std::string_view key,
                                             std::string fallback) const {
    const auto found = fields_.find(std::string(key));
    if (found == fields_.end()) {
      return fallback;
    }
    return found->second;
  }

  [[nodiscard]] Result<void> reject_unknown(
      std::initializer_list<std::string_view> allowed) const {
    for (const auto& entry : fields_) {
      bool known = false;
      for (const std::string_view candidate : allowed) {
        if (entry.first == candidate) {
          known = true;
          break;
        }
      }
      if (!known) {
        return line_error(ErrorCode::UnknownKeyword, line_,
                          "the record carries a field this format does not define")
            .with_subject(entry.first);
      }
    }
    return Ok{};
  }

  /// Keeps only the fields the parser did not consume, so the caller can decide
  /// whether a leftover is acceptable.
  [[nodiscard]] const std::map<std::string, std::string>& fields() const noexcept {
    return fields_;
  }

 private:
  std::string keyword_;
  std::size_t line_;
  std::map<std::string, std::string> fields_;
};

/// Splits one line into tokens, honouring double-quoted values.
[[nodiscard]] Result<std::vector<Field>> split_fields(std::string_view line,
                                                      std::size_t number) {
  std::vector<Field> fields;
  std::size_t index = 0;
  if (line.empty()) {
    // A record keyword with no fields at all: the caller decides whether that
    // is acceptable.
    return fields;
  }
  if (line.front() == ' ' || line.back() == ' ') {
    return line_error(ErrorCode::MalformedRecord, number,
                      "a record may not begin or end with a space");
  }
  while (index < line.size()) {
    const std::size_t start = index;
    while (index < line.size() && line[index] != ' ' && line[index] != '=') {
      index += 1;
    }
    if (index >= line.size() || line[index] != '=') {
      return line_error(ErrorCode::MalformedRecord, number,
                        "every field must be written as key=value");
    }
    Field field;
    field.key.assign(line.substr(start, index - start));
    index += 1;  // consume '='
    if (index < line.size() && line[index] == '"') {
      index += 1;
      bool closed = false;
      while (index < line.size()) {
        const char byte = line[index];
        if (byte == '\\') {
          if (index + 1 >= line.size()) {
            return line_error(ErrorCode::MalformedRecord, number,
                              "a quoted value ends with an escape character");
          }
          const char escaped = line[index + 1];
          if (escaped != '"' && escaped != '\\') {
            return line_error(ErrorCode::MalformedRecord, number,
                              "only quote and backslash may be escaped in a quoted value");
          }
          field.value.push_back(escaped);
          index += 2;
          continue;
        }
        if (byte == '"') {
          closed = true;
          index += 1;
          break;
        }
        field.value.push_back(byte);
        index += 1;
      }
      if (!closed) {
        return line_error(ErrorCode::MalformedRecord, number,
                          "a quoted value is not terminated");
      }
      if (index < line.size() && line[index] != ' ') {
        return line_error(ErrorCode::MalformedRecord, number,
                          "a quoted value must be followed by a space or the end of the line");
      }
    } else {
      const std::size_t value_start = index;
      while (index < line.size() && line[index] != ' ') {
        index += 1;
      }
      field.value.assign(line.substr(value_start, index - value_start));
    }
    if (field.key.empty()) {
      return line_error(ErrorCode::MalformedRecord, number, "a field has an empty key");
    }
    fields.push_back(std::move(field));
    if (index < line.size()) {
      if (line[index] != ' ') {
        return line_error(ErrorCode::MalformedRecord, number,
                          "fields are separated by exactly one space");
      }
      index += 1;
      if (index < line.size() && line[index] == ' ') {
        return line_error(ErrorCode::MalformedRecord, number,
                          "fields are separated by exactly one space");
      }
    }
  }
  return fields;
}

[[nodiscard]] Result<bool> parse_bool_field(const Record& record, std::string_view key,
                                            bool fallback, bool required) {
  if (!record.has(key)) {
    if (required) {
      return line_error(ErrorCode::MissingRequiredField, record.line(),
                        "the record does not carry a required field")
          .with_subject(std::string(key));
    }
    return fallback;
  }
  CCA_TRY_ASSIGN(text, record.require(key));
  if (text == "true") {
    return true;
  }
  if (text == "false") {
    return false;
  }
  return line_error(ErrorCode::ImpossibleEnumValue, record.line(),
                    "a boolean field is exactly true or false")
      .with_subject(std::string(key));
}

[[nodiscard]] Result<std::uint32_t> parse_u32_field(const Record& record,
                                                    std::string_view key,
                                                    std::uint32_t fallback,
                                                    bool required) {
  if (!record.has(key)) {
    if (required) {
      return line_error(ErrorCode::MissingRequiredField, record.line(),
                        "the record does not carry a required field")
          .with_subject(std::string(key));
    }
    return fallback;
  }
  CCA_TRY_ASSIGN(text, record.require(key));
  return parse_uint32(text);
}

[[nodiscard]] Result<std::uint64_t> parse_u64_field(const Record& record,
                                                    std::string_view key,
                                                    std::uint64_t fallback,
                                                    bool required) {
  if (!record.has(key)) {
    if (required) {
      return line_error(ErrorCode::MissingRequiredField, record.line(),
                        "the record does not carry a required field")
          .with_subject(std::string(key));
    }
    return fallback;
  }
  CCA_TRY_ASSIGN(text, record.require(key));
  return parse_uint64(text);
}

[[nodiscard]] Result<Timestamp> parse_timestamp_field(const Record& record,
                                                      std::string_view key,
                                                      Timestamp fallback, bool required) {
  if (!record.has(key)) {
    if (required) {
      return line_error(ErrorCode::MissingRequiredField, record.line(),
                        "the record does not carry a required field")
          .with_subject(std::string(key));
    }
    return fallback;
  }
  CCA_TRY_ASSIGN(text, record.require(key));
  return parse_utc(text);
}

[[nodiscard]] Result<BoundedText> parse_text_field(const Record& record,
                                                   std::string_view key) {
  if (!record.has(key)) {
    return BoundedText();
  }
  CCA_TRY_ASSIGN(text, record.require(key));
  return BoundedText::parse(text);
}

[[nodiscard]] Result<std::vector<std::string>> parse_list_field(const Record& record,
                                                                std::string_view key) {
  std::vector<std::string> values;
  if (!record.has(key)) {
    return values;
  }
  CCA_TRY_ASSIGN(text, record.require(key));
  if (text.empty()) {
    return values;
  }
  for (std::string& entry : split_list(text)) {
    if (entry.empty()) {
      return line_error(ErrorCode::MalformedRecord, record.line(),
                        "a list contains an empty element");
    }
    values.push_back(std::move(entry));
  }
  return values;
}

[[nodiscard]] Result<Measure<ThermalPower>> parse_power_measure(
    const Record& record, std::string_view known_key, std::string_view unknown_key,
    std::string_view unsupported_key, bool required) {
  const bool has_known = record.has(known_key);
  const bool has_unknown = record.has(unknown_key);
  const bool has_unsupported = record.has(unsupported_key);
  const int present = (has_known ? 1 : 0) + (has_unknown ? 1 : 0) + (has_unsupported ? 1 : 0);
  if (present > 1) {
    return line_error(ErrorCode::MalformedRecord, record.line(),
                      "a quantity is stated exactly once, as a value or as a reason");
  }
  if (present == 0) {
    if (required) {
      return line_error(ErrorCode::MissingRequiredField, record.line(),
                        "the record does not state the quantity")
          .with_subject(std::string(known_key));
    }
    BoundedText explanation;
    return Measure<ThermalPower>::unknown(MeasureReason::NotMeasured, explanation);
  }
  if (has_known) {
    CCA_TRY_ASSIGN(text, record.require(known_key));
    CCA_TRY_ASSIGN(value, parse_int64(text));
    CCA_TRY_ASSIGN(quantity, ThermalPower::checked_milliwatts(value));
    return Measure<ThermalPower>::known(quantity);
  }
  if (has_unknown) {
    CCA_TRY_ASSIGN(text, record.require(unknown_key));
    CCA_TRY_ASSIGN(reason, parse_measure_reason(text));
    if (reason == MeasureReason::None) {
      return line_error(ErrorCode::ImpossibleEnumValue, record.line(),
                        "an unknown quantity must state why it is unknown")
          .with_subject(std::string(unknown_key));
    }
    return Measure<ThermalPower>::unknown(reason, BoundedText());
  }
  CCA_TRY_ASSIGN(text, record.require(unsupported_key));
  CCA_TRY_ASSIGN(reason, parse_measure_reason(text));
  return Measure<ThermalPower>::unsupported(reason, BoundedText());
}

constexpr std::size_t kNoRank = 0;
constexpr std::size_t kGenerationsRank = 1;
constexpr std::size_t kPolicyRank = 2;
constexpr std::size_t kClassRank = 3;
constexpr std::size_t kEvidenceRank = 4;
constexpr std::size_t kDomainRank = 5;
constexpr std::size_t kScopeRank = 6;
constexpr std::size_t kGroupRank = 7;
constexpr std::size_t kContributionRank = 8;
constexpr std::size_t kManifestRank = 9;
constexpr std::size_t kEndRank = 10;

/// The subject of an unknown-keyword error is bounded: an attacker-supplied
/// line must not turn into an unbounded error string.
[[nodiscard]] std::string bounded_subject(std::string_view keyword) {
  constexpr std::size_t kSubjectLimit = 32;
  if (keyword.size() <= kSubjectLimit) {
    return std::string(keyword);
  }
  return std::string(keyword.substr(0, kSubjectLimit)) + "...";
}

[[nodiscard]] Result<std::size_t> rank_of(std::string_view keyword) {
  if (keyword == "GENERATIONS") {
    return kGenerationsRank;
  }
  if (keyword == "POLICY") {
    return kPolicyRank;
  }
  if (keyword == "CLASS") {
    return kClassRank;
  }
  if (keyword == "EVIDENCE") {
    return kEvidenceRank;
  }
  if (keyword == "DOMAIN") {
    return kDomainRank;
  }
  if (keyword == "SCOPE") {
    return kScopeRank;
  }
  if (keyword == "GROUP") {
    return kGroupRank;
  }
  if (keyword == "CONTRIBUTION" || keyword == "DERATE" || keyword == "SHARE") {
    return kContributionRank;
  }
  if (keyword == "MANIFEST") {
    return kManifestRank;
  }
  if (keyword == "END") {
    return kEndRank;
  }
  return Error::of(ErrorCode::UnknownKeyword, "the record keyword is not defined")
      .with_subject(bounded_subject(keyword))
      .with_detail("keyword length " +
                   to_decimal(static_cast<std::uint64_t>(keyword.size())));
}

[[nodiscard]] std::string quote_text(std::string_view text) {
  bool needs_quotes = text.empty();
  for (const char byte : text) {
    if (byte == ' ' || byte == '"' || byte == '\\') {
      needs_quotes = true;
      break;
    }
  }
  if (!needs_quotes) {
    return std::string(text);
  }
  std::string quoted;
  quoted.push_back('"');
  for (const char byte : text) {
    if (byte == '"' || byte == '\\') {
      quoted.push_back('\\');
    }
    quoted.push_back(byte);
  }
  quoted.push_back('"');
  return quoted;
}

void append_field(std::string& line, std::string_view key, std::string_view value) {
  line.push_back(' ');
  line.append(key);
  line.push_back('=');
  line.append(value);
}

template <typename T, typename Key>
void sort_records(std::vector<T>& values, Key key) {
  std::sort(values.begin(), values.end(),
            [&key](const T& lhs, const T& rhs) { return key(lhs) < key(rhs); });
}

}  // namespace


Result<AccountingInput> parse_interchange(std::string_view text, const Limits& limits) {
  CCA_TRY(limits.validate());
  AccountingInput input;
  bool saw_header = false;
  bool saw_generations = false;
  bool saw_policy = false;
  bool saw_end = false;
  std::size_t current_rank = kNoRank;
  std::size_t line_number = 0;
  std::string last_contribution;
  bool last_contribution_open = false;

  std::size_t offset = 0;
  while (offset <= text.size()) {
    if (offset == text.size()) {
      break;
    }
    std::size_t end = text.find('\n', offset);
    std::string_view line;
    if (end == std::string_view::npos) {
      line = text.substr(offset);
      offset = text.size();
    } else {
      line = text.substr(offset, end - offset);
      offset = end + 1;
    }
    line_number += 1;
    if (line_number > kMaxInterchangeLines) {
      return line_error(ErrorCode::TooManyItems, line_number,
                        "the interchange text has more lines than the bound allows");
    }
    if (line.size() > kMaxLineLength) {
      return line_error(ErrorCode::ItemTooLarge, line_number,
                        "a line is longer than the bound allows");
    }
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (line.empty()) {
      return line_error(ErrorCode::MalformedRecord, line_number,
                        "the format has no empty lines");
    }
    if (saw_end) {
      return line_error(ErrorCode::TrailingBytes, line_number,
                        "the interchange text continues after END");
    }

    constexpr std::string_view kHeaderKeyword = "CCA-INTERCHANGE";
    if (line.rfind(kHeaderKeyword, 0) == 0) {
      if (saw_header || line_number != 1) {
        return line_error(ErrorCode::RecordOrderViolation, line_number,
                          "the header must be the first line and appear once");
      }
      if (line.size() <= kHeaderKeyword.size() ||
          line[kHeaderKeyword.size()] != ' ') {
        return line_error(ErrorCode::MalformedRecord, line_number,
                          "the header line is exactly: CCA-INTERCHANGE <version>");
      }
      const std::string_view version_text = line.substr(kHeaderKeyword.size() + 1U);
      if (version_text.find(' ') != std::string_view::npos) {
        return line_error(ErrorCode::MalformedRecord, line_number,
                          "the header line is exactly: CCA-INTERCHANGE <version>");
      }
      CCA_TRY_ASSIGN(version, parse_uint32(version_text));
      if (version != kInterchangeFormatVersion) {
        return line_error(ErrorCode::UnsupportedFormatVersion, line_number,
                          "the interchange format version is not supported");
      }
      saw_header = true;
      current_rank = kNoRank;
      continue;
    }
    if (!saw_header) {
      return line_error(ErrorCode::RecordOrderViolation, line_number,
                        "the header must come first");
    }

    const std::size_t separator = line.find(' ');
    const std::string keyword(line.substr(0, separator));
    const std::string_view remainder =
        separator == std::string_view::npos ? std::string_view()
                                            : line.substr(separator + 1U);

    if (keyword.find('=') != std::string::npos) {
      return line_error(ErrorCode::MalformedRecord, line_number,
                        "a record starts with a keyword, not with a field");
    }
    CCA_TRY_ASSIGN(rank, rank_of(keyword));
    if (rank < current_rank) {
      return line_error(ErrorCode::RecordOrderViolation, line_number,
                        "the record appears after a record it must precede");
    }
    current_rank = rank;
    CCA_TRY_ASSIGN(fields, split_fields(remainder, line_number));

    if (keyword == "END") {
      if (!fields.empty()) {
        return line_error(ErrorCode::MalformedRecord, line_number,
                          "END carries no fields");
      }
      if (!saw_generations || !saw_policy) {
        return line_error(ErrorCode::MissingRequiredField, line_number,
                          "GENERATIONS and POLICY are required before END");
      }
      saw_end = true;
      continue;
    }

    Record record(keyword, line_number);
    for (const Field& field : fields) {
      CCA_TRY(record.add(field.key, field.value));
    }

    if (keyword == "GENERATIONS") {
      if (saw_generations) {
        return line_error(ErrorCode::DuplicateField, line_number,
                          "GENERATIONS appears more than once");
      }
      CCA_TRY(record.reject_unknown({"epoch", "topology", "policy", "evidence",
                                     "revision"}));
      CCA_TRY_ASSIGN(epoch, parse_u64_field(record, "epoch", 0, true));
      CCA_TRY_ASSIGN(topology, parse_u64_field(record, "topology", 0, true));
      CCA_TRY_ASSIGN(policy, parse_u64_field(record, "policy", 0, true));
      CCA_TRY_ASSIGN(evidence, parse_u64_field(record, "evidence", 0, true));
      CCA_TRY_ASSIGN(revision, parse_u64_field(record, "revision", 0, true));
      CCA_TRY_ASSIGN(epoch_value, ControlPlaneEpoch::of(epoch));
      CCA_TRY_ASSIGN(topology_value, TopologyGeneration::of(topology));
      CCA_TRY_ASSIGN(policy_value, PolicyGeneration::of(policy));
      CCA_TRY_ASSIGN(evidence_value, EvidenceGeneration::of(evidence));
      CCA_TRY_ASSIGN(revision_value, StateRevision::of(revision));
      input.generations.epoch = epoch_value;
      input.generations.topology = topology_value;
      input.generations.policy = policy_value;
      input.generations.evidence = evidence_value;
      input.generations.revision = revision_value;
      saw_generations = true;
      continue;
    }

    if (keyword == "POLICY") {
      if (saw_policy) {
        return line_error(ErrorCode::DuplicateField, line_number,
                          "POLICY appears more than once");
      }
      CCA_TRY(record.reject_unknown(
          {"id", "generation", "epoch", "effective_from", "max_evidence_age_ms",
           "minimum_coverage_ppm", "residual_tolerance_ppm", "max_derate_factors",
           "retention_generations", "out_of_service_requires_evidence",
           "classification_required", "installed_evidence_required",
           "derate_evidence_required", "note"}));
      CCA_TRY_ASSIGN(id_text, record.require("id"));
      CCA_TRY_ASSIGN(id, PolicyId::parse(id_text));
      input.policy.id = id;
      CCA_TRY_ASSIGN(generation, parse_u64_field(record, "generation", 0, true));
      CCA_TRY_ASSIGN(generation_value, PolicyGeneration::of(generation));
      input.policy.generation = generation_value;
      CCA_TRY_ASSIGN(epoch, parse_u64_field(record, "epoch", 0, true));
      CCA_TRY_ASSIGN(epoch_value, ControlPlaneEpoch::of(epoch));
      input.policy.epoch = epoch_value;
      CCA_TRY_ASSIGN(effective_from,
                     parse_timestamp_field(record, "effective_from", Timestamp::epoch(),
                                           true));
      input.policy.effective_from = effective_from;
      CCA_TRY_ASSIGN(age, parse_u64_field(record, "max_evidence_age_ms", 0, true));
      CCA_TRY_ASSIGN(age_value, DurationMs::checked_milliseconds(
                                    static_cast<std::int64_t>(age)));
      input.policy.max_evidence_age = age_value;
      CCA_TRY_ASSIGN(coverage, parse_u32_field(record, "minimum_coverage_ppm", 0, true));
      CCA_TRY_ASSIGN(coverage_value, Ratio::of_ppm(coverage));
      input.policy.minimum_coverage = coverage_value;
      CCA_TRY_ASSIGN(tolerance,
                     parse_u32_field(record, "residual_tolerance_ppm", 0, true));
      CCA_TRY_ASSIGN(tolerance_value, Ratio::of_ppm(tolerance));
      input.policy.residual_tolerance = tolerance_value;
      CCA_TRY_ASSIGN(derates, parse_u64_field(record, "max_derate_factors", 1, true));
      input.policy.max_derate_factors = static_cast<std::size_t>(derates);
      CCA_TRY_ASSIGN(retention,
                     parse_u64_field(record, "retention_generations", 1, true));
      input.policy.retention_generations = static_cast<std::size_t>(retention);
      CCA_TRY_ASSIGN(out_of_service,
                     parse_bool_field(record, "out_of_service_requires_evidence", true,
                                      true));
      input.policy.require_out_of_service_evidence = out_of_service;
      CCA_TRY_ASSIGN(classification,
                     parse_bool_field(record, "classification_required", true, true));
      input.policy.require_classification = classification;
      CCA_TRY_ASSIGN(installed,
                     parse_bool_field(record, "installed_evidence_required", true, true));
      input.policy.require_installed_evidence = installed;
      CCA_TRY_ASSIGN(derate_evidence,
                     parse_bool_field(record, "derate_evidence_required", true, true));
      input.policy.require_derate_evidence = derate_evidence;
      CCA_TRY_ASSIGN(note, parse_text_field(record, "note"));
      input.policy.note = note;
      saw_policy = true;
      continue;
    }

    if (keyword == "CLASS") {
      CCA_TRY(record.reject_unknown({"id", "kind", "medium", "label", "contributes"}));
      EquipmentClass klass;
      CCA_TRY_ASSIGN(id_text, record.require("id"));
      CCA_TRY_ASSIGN(id, EquipmentClassId::parse(id_text));
      klass.id = id;
      CCA_TRY_ASSIGN(kind_text, record.require("kind"));
      CCA_TRY_ASSIGN(kind, parse_equipment_class_kind(kind_text));
      klass.kind = kind;
      CCA_TRY_ASSIGN(medium_text, record.require("medium"));
      CCA_TRY_ASSIGN(medium, parse_medium(medium_text));
      klass.medium = medium;
      CCA_TRY_ASSIGN(label, parse_text_field(record, "label"));
      klass.label = label;
      CCA_TRY_ASSIGN(contributes,
                     parse_bool_field(record, "contributes", true, false));
      klass.contributes_to_installed = contributes;
      input.equipment_classes.push_back(std::move(klass));
      continue;
    }

    if (keyword == "EVIDENCE") {
      CCA_TRY(record.reject_unknown(
          {"id", "kind", "source_kind", "source", "observed_at", "recorded_at",
           "evidence_generation", "epoch", "topology", "policy", "medium", "value_mw",
           "value_unknown", "value_unsupported", "subject_scope", "subject_equipment",
           "reference", "label", "notes", "sequence", "content_digest"}));
      EvidenceRecord entry;
      CCA_TRY_ASSIGN(id_text, record.require("id"));
      CCA_TRY_ASSIGN(id, EvidenceId::parse(id_text));
      entry.id = id;
      CCA_TRY_ASSIGN(kind_text, record.require("kind"));
      CCA_TRY_ASSIGN(kind, parse_evidence_kind(kind_text));
      entry.kind = kind;
      CCA_TRY_ASSIGN(source_kind_text, record.require("source_kind"));
      CCA_TRY_ASSIGN(source_kind, parse_evidence_source_kind(source_kind_text));
      entry.source_kind = source_kind;
      CCA_TRY_ASSIGN(source_text, record.require("source"));
      CCA_TRY_ASSIGN(source, EvidenceSourceId::parse(source_text));
      entry.source = source;
      CCA_TRY_ASSIGN(observed_at,
                     parse_timestamp_field(record, "observed_at", Timestamp::epoch(),
                                           true));
      entry.observed_at = observed_at;
      CCA_TRY_ASSIGN(recorded_at,
                     parse_timestamp_field(record, "recorded_at", Timestamp::epoch(),
                                           true));
      entry.recorded_at = recorded_at;
      CCA_TRY_ASSIGN(evidence_generation,
                     parse_u64_field(record, "evidence_generation", 0, true));
      CCA_TRY_ASSIGN(evidence_value, EvidenceGeneration::of(evidence_generation));
      entry.binding.evidence = evidence_value;
      CCA_TRY_ASSIGN(epoch, parse_u64_field(record, "epoch", 0, true));
      CCA_TRY_ASSIGN(epoch_value, ControlPlaneEpoch::of(epoch));
      entry.binding.epoch = epoch_value;
      CCA_TRY_ASSIGN(topology, parse_u64_field(record, "topology", 0, true));
      CCA_TRY_ASSIGN(topology_value, TopologyGeneration::of(topology));
      entry.binding.topology = topology_value;
      CCA_TRY_ASSIGN(policy_generation, parse_u64_field(record, "policy", 0, true));
      CCA_TRY_ASSIGN(policy_value, PolicyGeneration::of(policy_generation));
      entry.binding.policy = policy_value;
      CCA_TRY_ASSIGN(medium_text, record.require("medium"));
      CCA_TRY_ASSIGN(medium, parse_medium(medium_text));
      entry.medium = medium;
      CCA_TRY_ASSIGN(value,
                     parse_power_measure(record, "value_mw", "value_unknown",
                                         "value_unsupported", false));
      entry.declared_value = value;
      CCA_TRY_ASSIGN(sequence, parse_u64_field(record, "sequence", 0, false));
      CCA_TRY_ASSIGN(sequence_value, ObservationSequence::of(sequence));
      entry.sequence = sequence_value;
      if (record.has("subject_scope")) {
        CCA_TRY_ASSIGN(text_value, record.require("subject_scope"));
        CCA_TRY_ASSIGN(scope_id, ScopeId::parse(text_value));
        entry.subject_scope = scope_id;
      }
      if (record.has("subject_equipment")) {
        CCA_TRY_ASSIGN(text_value, record.require("subject_equipment"));
        CCA_TRY_ASSIGN(equipment_id, EquipmentId::parse(text_value));
        entry.subject_equipment = equipment_id;
      }
      if (record.has("reference")) {
        CCA_TRY_ASSIGN(text_value, record.require("reference"));
        CCA_TRY_ASSIGN(reference, DocumentRef::parse(text_value));
        entry.reference = reference;
      }
      CCA_TRY_ASSIGN(label, parse_text_field(record, "label"));
      entry.label = label;
      CCA_TRY_ASSIGN(notes, parse_text_field(record, "notes"));
      entry.notes = notes;
      if (record.has("content_digest")) {
        CCA_TRY_ASSIGN(text_value, record.require("content_digest"));
        CCA_TRY_ASSIGN(digest, Digest::parse_hex(text_value));
        entry.content_digest = digest;
      }
      input.evidence.push_back(std::move(entry));
      continue;
    }

    if (keyword == "DOMAIN") {
      CCA_TRY(record.reject_unknown({"id", "scope", "evidence", "label", "epoch",
                                     "topology"}));
      IndependenceDomain domain;
      CCA_TRY_ASSIGN(id_text, record.require("id"));
      CCA_TRY_ASSIGN(id, IndependenceDomainId::parse(id_text));
      domain.id = id;
      CCA_TRY_ASSIGN(scope_text, record.require("scope"));
      CCA_TRY_ASSIGN(scope, ScopeId::parse(scope_text));
      domain.scope = scope;
      if (record.has("evidence")) {
        CCA_TRY_ASSIGN(text_value, record.require("evidence"));
        CCA_TRY_ASSIGN(evidence_id, EvidenceId::parse(text_value));
        domain.evidence = evidence_id;
      }
      CCA_TRY_ASSIGN(label, parse_text_field(record, "label"));
      domain.label = label;
      CCA_TRY_ASSIGN(epoch, parse_u64_field(record, "epoch",
                                            input.generations.epoch.value(), false));
      CCA_TRY_ASSIGN(epoch_value, ControlPlaneEpoch::of(epoch));
      domain.epoch = epoch_value;
      CCA_TRY_ASSIGN(topology,
                     parse_u64_field(record, "topology",
                                     input.generations.topology.value(), false));
      CCA_TRY_ASSIGN(topology_value, TopologyGeneration::of(topology));
      domain.topology_generation = topology_value;
      input.independence_domains.push_back(std::move(domain));
      continue;
    }

    if (keyword == "SCOPE") {
      CCA_TRY(record.reject_unknown({"id", "kind", "medium", "parent", "label",
                                     "classes", "declared_mw", "declared_unknown",
                                     "declared_unsupported", "declared_evidence",
                                     "epoch", "topology"}));
      AccountingScope scope;
      CCA_TRY_ASSIGN(id_text, record.require("id"));
      CCA_TRY_ASSIGN(id, ScopeId::parse(id_text));
      scope.id = id;
      CCA_TRY_ASSIGN(kind_text, record.require("kind"));
      CCA_TRY_ASSIGN(kind, parse_scope_kind(kind_text));
      scope.kind = kind;
      CCA_TRY_ASSIGN(medium_text, record.require("medium"));
      CCA_TRY_ASSIGN(medium, parse_medium(medium_text));
      scope.medium = medium;
      if (record.has("parent")) {
        CCA_TRY_ASSIGN(text_value, record.require("parent"));
        CCA_TRY_ASSIGN(parent, ScopeId::parse(text_value));
        scope.parent = parent;
      }
      CCA_TRY_ASSIGN(label, parse_text_field(record, "label"));
      scope.label = label;
      CCA_TRY_ASSIGN(class_names, parse_list_field(record, "classes"));
      for (const std::string& name : class_names) {
        CCA_TRY_ASSIGN(class_id, EquipmentClassId::parse(name));
        scope.classes.push_back(class_id);
      }
      CCA_TRY_ASSIGN(declared,
                     parse_power_measure(record, "declared_mw", "declared_unknown",
                                         "declared_unsupported", false));
      scope.declared_installed_total = declared;
      CCA_TRY_ASSIGN(evidence_names, parse_list_field(record, "declared_evidence"));
      for (const std::string& name : evidence_names) {
        CCA_TRY_ASSIGN(evidence_id, EvidenceId::parse(name));
        scope.declared_total_evidence.push_back(evidence_id);
      }
      CCA_TRY_ASSIGN(epoch, parse_u64_field(record, "epoch",
                                            input.generations.epoch.value(), false));
      CCA_TRY_ASSIGN(epoch_value, ControlPlaneEpoch::of(epoch));
      scope.epoch = epoch_value;
      CCA_TRY_ASSIGN(topology,
                     parse_u64_field(record, "topology",
                                     input.generations.topology.value(), false));
      CCA_TRY_ASSIGN(topology_value, TopologyGeneration::of(topology));
      scope.topology_generation = topology_value;
      input.scopes.push_back(std::move(scope));
      continue;
    }

    if (keyword == "GROUP") {
      CCA_TRY(record.reject_unknown(
          {"id", "scope", "classification", "medium", "required", "redundancy",
           "protected_mw", "protected_unknown", "protected_unsupported",
           "protected_evidence", "domains", "epoch", "topology", "policy",
           "evidence_generation"}));
      ContributionGroup group;
      CCA_TRY_ASSIGN(id_text, record.require("id"));
      CCA_TRY_ASSIGN(id, ContributionGroupId::parse(id_text));
      group.id = id;
      CCA_TRY_ASSIGN(scope_text, record.require("scope"));
      CCA_TRY_ASSIGN(scope, ScopeId::parse(scope_text));
      group.scope = scope;
      CCA_TRY_ASSIGN(classification_text, record.require("classification"));
      CCA_TRY_ASSIGN(classification, parse_contribution_class(classification_text));
      group.classification = classification;
      CCA_TRY_ASSIGN(medium_text, record.require("medium"));
      CCA_TRY_ASSIGN(medium, parse_medium(medium_text));
      group.medium = medium;
      CCA_TRY_ASSIGN(required, parse_u32_field(record, "required", 0, false));
      group.required_concurrent = required;
      if (record.has("redundancy")) {
        CCA_TRY_ASSIGN(text_value, record.require("redundancy"));
        CCA_TRY_ASSIGN(redundancy, parse_redundancy_class(text_value));
        group.redundancy = redundancy;
      }
      CCA_TRY_ASSIGN(protected_quantity,
                     parse_power_measure(record, "protected_mw", "protected_unknown",
                                         "protected_unsupported", false));
      group.protected_quantity = protected_quantity;
      if (record.has("protected_evidence")) {
        CCA_TRY_ASSIGN(text_value, record.require("protected_evidence"));
        CCA_TRY_ASSIGN(evidence_id, EvidenceId::parse(text_value));
        group.protected_quantity_evidence = evidence_id;
      }
      CCA_TRY_ASSIGN(domain_names, parse_list_field(record, "domains"));
      for (const std::string& name : domain_names) {
        CCA_TRY_ASSIGN(domain_id, IndependenceDomainId::parse(name));
        group.independence_domains.push_back(domain_id);
      }
      CCA_TRY_ASSIGN(epoch, parse_u64_field(record, "epoch",
                                            input.generations.epoch.value(), false));
      CCA_TRY_ASSIGN(epoch_value, ControlPlaneEpoch::of(epoch));
      group.binding.epoch = epoch_value;
      CCA_TRY_ASSIGN(topology,
                     parse_u64_field(record, "topology",
                                     input.generations.topology.value(), false));
      CCA_TRY_ASSIGN(topology_value, TopologyGeneration::of(topology));
      group.binding.topology = topology_value;
      CCA_TRY_ASSIGN(policy_generation,
                     parse_u64_field(record, "policy",
                                     input.generations.policy.value(), false));
      CCA_TRY_ASSIGN(policy_value, PolicyGeneration::of(policy_generation));
      group.binding.policy = policy_value;
      CCA_TRY_ASSIGN(evidence_generation,
                     parse_u64_field(record, "evidence_generation",
                                     input.generations.evidence.value(), false));
      CCA_TRY_ASSIGN(evidence_value, EvidenceGeneration::of(evidence_generation));
      group.binding.evidence = evidence_value;
      input.groups.push_back(std::move(group));
      continue;
    }

    if (keyword == "CONTRIBUTION") {
      CCA_TRY(record.reject_unknown(
          {"id", "scope", "equipment", "class", "medium", "classification", "service",
           "installed_mw", "installed_unknown", "installed_unsupported", "priority",
           "group", "domain", "aliases", "epoch", "topology", "policy",
           "evidence_generation", "evidence", "evidence_refs", "observed_at",
           "sequence", "label"}));
      Contribution contribution;
      CCA_TRY_ASSIGN(id_text, record.require("id"));
      CCA_TRY_ASSIGN(id, ContributionId::parse(id_text));
      contribution.id = id;
      CCA_TRY_ASSIGN(scope_text, record.require("scope"));
      CCA_TRY_ASSIGN(scope, ScopeId::parse(scope_text));
      contribution.home_scope = scope;
      CCA_TRY_ASSIGN(equipment_text, record.require("equipment"));
      CCA_TRY_ASSIGN(equipment, EquipmentId::parse(equipment_text));
      contribution.equipment = equipment;
      CCA_TRY_ASSIGN(class_text, record.require("class"));
      CCA_TRY_ASSIGN(class_id, EquipmentClassId::parse(class_text));
      contribution.equipment_class = class_id;
      CCA_TRY_ASSIGN(medium_text, record.require("medium"));
      CCA_TRY_ASSIGN(medium, parse_medium(medium_text));
      contribution.medium = medium;
      CCA_TRY_ASSIGN(classification_text, record.require("classification"));
      CCA_TRY_ASSIGN(classification, parse_contribution_class(classification_text));
      contribution.classification = classification;
      CCA_TRY_ASSIGN(service_text, record.require("service"));
      CCA_TRY_ASSIGN(service, parse_service_state(service_text));
      contribution.service = service;
      CCA_TRY_ASSIGN(installed,
                     parse_power_measure(record, "installed_mw", "installed_unknown",
                                         "installed_unsupported", true));
      contribution.installed = installed;
      CCA_TRY_ASSIGN(priority, parse_u32_field(record, "priority", 0, false));
      contribution.priority = priority;
      if (record.has("group")) {
        CCA_TRY_ASSIGN(text_value, record.require("group"));
        CCA_TRY_ASSIGN(group_id, ContributionGroupId::parse(text_value));
        contribution.group = group_id;
      }
      if (record.has("domain")) {
        CCA_TRY_ASSIGN(text_value, record.require("domain"));
        CCA_TRY_ASSIGN(domain_id, IndependenceDomainId::parse(text_value));
        contribution.independence_domain = domain_id;
      }
      CCA_TRY_ASSIGN(alias_names, parse_list_field(record, "aliases"));
      for (const std::string& name : alias_names) {
        CCA_TRY_ASSIGN(alias, ScopeId::parse(name));
        contribution.aliases.push_back(alias);
      }
      CCA_TRY_ASSIGN(epoch, parse_u64_field(record, "epoch",
                                            input.generations.epoch.value(), false));
      CCA_TRY_ASSIGN(epoch_value, ControlPlaneEpoch::of(epoch));
      contribution.binding.epoch = epoch_value;
      CCA_TRY_ASSIGN(topology,
                     parse_u64_field(record, "topology",
                                     input.generations.topology.value(), false));
      CCA_TRY_ASSIGN(topology_value, TopologyGeneration::of(topology));
      contribution.binding.topology = topology_value;
      CCA_TRY_ASSIGN(policy_generation,
                     parse_u64_field(record, "policy",
                                     input.generations.policy.value(), false));
      CCA_TRY_ASSIGN(policy_value, PolicyGeneration::of(policy_generation));
      contribution.binding.policy = policy_value;
      CCA_TRY_ASSIGN(evidence_generation,
                     parse_u64_field(record, "evidence_generation",
                                     input.generations.evidence.value(), false));
      CCA_TRY_ASSIGN(evidence_value, EvidenceGeneration::of(evidence_generation));
      contribution.binding.evidence = evidence_value;
      if (record.has("evidence")) {
        CCA_TRY_ASSIGN(text_value, record.require("evidence"));
        CCA_TRY_ASSIGN(evidence_id, EvidenceId::parse(text_value));
        contribution.primary_evidence = evidence_id;
      }
      CCA_TRY_ASSIGN(reference_names, parse_list_field(record, "evidence_refs"));
      for (const std::string& name : reference_names) {
        CCA_TRY_ASSIGN(evidence_id, EvidenceId::parse(name));
        contribution.evidence.push_back(evidence_id);
      }
      CCA_TRY_ASSIGN(observed_at,
                     parse_timestamp_field(record, "observed_at", Timestamp::epoch(),
                                           true));
      contribution.observed_at = observed_at;
      CCA_TRY_ASSIGN(sequence, parse_u64_field(record, "sequence", 0, false));
      CCA_TRY_ASSIGN(sequence_value, ObservationSequence::of(sequence));
      contribution.observation_sequence = sequence_value;
      last_contribution = contribution.id.str();
      last_contribution_open = true;
      input.contributions.push_back(std::move(contribution));
      continue;
    }

    if (keyword == "DERATE" || keyword == "SHARE") {
      if (!last_contribution_open) {
        return line_error(ErrorCode::UnknownReference, line_number,
                          "the record does not follow a contribution");
      }
      CCA_TRY_ASSIGN(target_text, record.require("contribution"));
      if (target_text != last_contribution) {
        return line_error(ErrorCode::UnknownReference, line_number,
                          "the record names a contribution that has not been read")
            .with_subject(target_text);
      }
      for (Contribution& contribution : input.contributions) {
        if (contribution.id.str() != last_contribution) {
          continue;
        }
        if (keyword == "DERATE") {
          CCA_TRY(record.reject_unknown(
              {"contribution", "id", "kind", "ppm", "mw", "evidence", "label"}));
          DerateFactor derate;
          CCA_TRY_ASSIGN(id_text, record.require("id"));
          CCA_TRY_ASSIGN(id, DerateId::parse(id_text));
          derate.id = id;
          CCA_TRY_ASSIGN(kind_text, record.require("kind"));
          CCA_TRY_ASSIGN(kind, parse_derate_kind(kind_text));
          derate.kind = kind;
          if (kind == DerateKind::Factor) {
            if (record.has("mw")) {
              return line_error(ErrorCode::MalformedRecord, line_number,
                                "a factor derate carries ppm, not mw");
            }
            CCA_TRY_ASSIGN(ppm, parse_u32_field(record, "ppm", 0, true));
            CCA_TRY_ASSIGN(factor, Ratio::of_ppm(ppm));
            derate.factor = factor;
          } else {
            if (record.has("ppm")) {
              return line_error(ErrorCode::MalformedRecord, line_number,
                                "an absolute derate carries mw, not ppm");
            }
            CCA_TRY_ASSIGN(mw, parse_u64_field(record, "mw", 0, true));
            CCA_TRY_ASSIGN(absolute, ThermalPower::checked_milliwatts(
                                         static_cast<std::int64_t>(mw)));
            derate.absolute = absolute;
            derate.factor = Ratio::one();
          }
          if (record.has("evidence")) {
            CCA_TRY_ASSIGN(text_value, record.require("evidence"));
            CCA_TRY_ASSIGN(evidence_id, EvidenceId::parse(text_value));
            derate.evidence = evidence_id;
          }
          CCA_TRY_ASSIGN(label, parse_text_field(record, "label"));
          derate.label = label;
          contribution.derates.push_back(std::move(derate));
        } else {
          CCA_TRY(record.reject_unknown({"contribution", "target", "ppm"}));
          ApportionmentShare share;
          CCA_TRY_ASSIGN(target_scope_text, record.require("target"));
          CCA_TRY_ASSIGN(target_scope, ScopeId::parse(target_scope_text));
          share.target = target_scope;
          CCA_TRY_ASSIGN(ppm, parse_u32_field(record, "ppm", 0, true));
          CCA_TRY_ASSIGN(ratio, Ratio::of_ppm(ppm));
          share.share = ratio;
          contribution.sharing.kind = Sharing::Kind::Apportioned;
          contribution.sharing.shares.push_back(std::move(share));
        }
        break;
      }
      continue;
    }

    if (keyword == "MANIFEST") {
      CCA_TRY(record.reject_unknown(
          {"id", "scope", "medium", "declared_mw", "evidence", "class", "observed_at",
           "epoch", "topology", "policy", "evidence_generation"}));
      ManifestDeclaration declaration;
      CCA_TRY_ASSIGN(id_text, record.require("id"));
      CCA_TRY_ASSIGN(id, ManifestId::parse(id_text));
      declaration.id = id;
      CCA_TRY_ASSIGN(scope_text, record.require("scope"));
      CCA_TRY_ASSIGN(scope, ScopeId::parse(scope_text));
      declaration.scope = scope;
      CCA_TRY_ASSIGN(medium_text, record.require("medium"));
      CCA_TRY_ASSIGN(medium, parse_medium(medium_text));
      declaration.medium = medium;
      CCA_TRY_ASSIGN(declared, parse_u64_field(record, "declared_mw", 0, true));
      CCA_TRY_ASSIGN(declared_value, ThermalPower::checked_milliwatts(
                                         static_cast<std::int64_t>(declared)));
      declaration.declared_installed = declared_value;
      CCA_TRY_ASSIGN(evidence_text, record.require("evidence"));
      CCA_TRY_ASSIGN(evidence_id, EvidenceId::parse(evidence_text));
      declaration.evidence = evidence_id;
      if (record.has("class")) {
        CCA_TRY_ASSIGN(text_value, record.require("class"));
        CCA_TRY_ASSIGN(class_id, EquipmentClassId::parse(text_value));
        declaration.equipment_class = class_id;
      }
      CCA_TRY_ASSIGN(observed_at,
                     parse_timestamp_field(record, "observed_at", Timestamp::epoch(),
                                           false));
      declaration.observed_at = observed_at;
      CCA_TRY_ASSIGN(epoch, parse_u64_field(record, "epoch",
                                            input.generations.epoch.value(), false));
      CCA_TRY_ASSIGN(epoch_value, ControlPlaneEpoch::of(epoch));
      declaration.binding.epoch = epoch_value;
      CCA_TRY_ASSIGN(topology,
                     parse_u64_field(record, "topology",
                                     input.generations.topology.value(), false));
      CCA_TRY_ASSIGN(topology_value, TopologyGeneration::of(topology));
      declaration.binding.topology = topology_value;
      CCA_TRY_ASSIGN(policy_generation,
                     parse_u64_field(record, "policy",
                                     input.generations.policy.value(), false));
      CCA_TRY_ASSIGN(policy_value, PolicyGeneration::of(policy_generation));
      declaration.binding.policy = policy_value;
      CCA_TRY_ASSIGN(evidence_generation,
                     parse_u64_field(record, "evidence_generation",
                                     input.generations.evidence.value(), false));
      CCA_TRY_ASSIGN(evidence_value, EvidenceGeneration::of(evidence_generation));
      declaration.binding.evidence = evidence_value;
      input.manifest_declarations.push_back(std::move(declaration));
      continue;
    }

    return line_error(ErrorCode::UnknownKeyword, line_number,
                      "the record keyword is not defined")
        .with_subject(keyword);
  }

  if (!saw_header) {
    return Error::of(ErrorCode::MissingRequiredField,
                     "the interchange text has no header line");
  }
  if (!saw_end) {
    return Error::of(ErrorCode::TruncatedInput,
                     "the interchange text does not end with END");
  }
  return input;
}

Result<std::string> write_interchange(const AccountingInput& input,
                                      const Limits& limits) {
  CCA_TRY(limits.validate());
  std::string text;
  text += "CCA-INTERCHANGE ";
  text += to_decimal(static_cast<std::uint64_t>(kInterchangeFormatVersion));
  text += "\n";

  std::string line = "GENERATIONS";
  append_field(line, "epoch", to_decimal(input.generations.epoch.value()));
  append_field(line, "topology", to_decimal(input.generations.topology.value()));
  append_field(line, "policy", to_decimal(input.generations.policy.value()));
  append_field(line, "evidence", to_decimal(input.generations.evidence.value()));
  append_field(line, "revision", to_decimal(input.generations.revision.value()));
  text += line;
  text += "\n";

  line = "POLICY";
  append_field(line, "id", input.policy.id.view());
  append_field(line, "generation", to_decimal(input.policy.generation.value()));
  append_field(line, "epoch", to_decimal(input.policy.epoch.value()));
  append_field(line, "effective_from", format_utc(input.policy.effective_from));
  append_field(line, "max_evidence_age_ms",
               to_decimal(input.policy.max_evidence_age.milliseconds()));
  append_field(line, "minimum_coverage_ppm",
               to_decimal(static_cast<std::uint64_t>(input.policy.minimum_coverage.ppm())));
  append_field(line, "residual_tolerance_ppm",
               to_decimal(static_cast<std::uint64_t>(input.policy.residual_tolerance.ppm())));
  append_field(line, "max_derate_factors",
               to_decimal(static_cast<std::uint64_t>(input.policy.max_derate_factors)));
  append_field(line, "retention_generations",
               to_decimal(static_cast<std::uint64_t>(input.policy.retention_generations)));
  append_field(line, "out_of_service_requires_evidence",
               input.policy.require_out_of_service_evidence ? "true" : "false");
  append_field(line, "classification_required",
               input.policy.require_classification ? "true" : "false");
  append_field(line, "installed_evidence_required",
               input.policy.require_installed_evidence ? "true" : "false");
  append_field(line, "derate_evidence_required",
               input.policy.require_derate_evidence ? "true" : "false");
  if (!input.policy.note.empty()) {
    append_field(line, "note", quote_text(input.policy.note.view()));
  }
  text += line;
  text += "\n";

  std::vector<EquipmentClass> classes = input.equipment_classes;
  sort_records(classes, [](const EquipmentClass& value) { return value.id; });
  for (const EquipmentClass& klass : classes) {
    line = "CLASS";
    append_field(line, "id", klass.id.view());
    append_field(line, "kind", equipment_class_kind_name(klass.kind));
    append_field(line, "medium", medium_name(klass.medium));
    append_field(line, "contributes", klass.contributes_to_installed ? "true" : "false");
    if (!klass.label.empty()) {
      append_field(line, "label", quote_text(klass.label.view()));
    }
    text += line;
    text += "\n";
  }

  std::vector<EvidenceRecord> evidence = input.evidence;
  sort_records(evidence, [](const EvidenceRecord& value) { return value.id; });
  for (const EvidenceRecord& entry : evidence) {
    line = "EVIDENCE";
    append_field(line, "id", entry.id.view());
    append_field(line, "kind", evidence_kind_name(entry.kind));
    append_field(line, "source_kind", evidence_source_kind_name(entry.source_kind));
    append_field(line, "source", entry.source.view());
    append_field(line, "observed_at", format_utc(entry.observed_at));
    append_field(line, "recorded_at", format_utc(entry.recorded_at));
    append_field(line, "evidence_generation", to_decimal(entry.binding.evidence.value()));
    append_field(line, "epoch", to_decimal(entry.binding.epoch.value()));
    append_field(line, "topology", to_decimal(entry.binding.topology.value()));
    append_field(line, "policy", to_decimal(entry.binding.policy.value()));
    append_field(line, "medium", medium_name(entry.medium));
    if (entry.declared_value.is_known()) {
      append_field(line, "value_mw",
                   to_decimal(entry.declared_value.value().milliwatts()));
    } else if (entry.declared_value.is_unknown()) {
      append_field(line, "value_unknown", measure_reason_name(entry.declared_value.reason()));
    } else {
      append_field(line, "value_unsupported",
                   measure_reason_name(entry.declared_value.reason()));
    }
    if (!entry.content_digest.is_zero()) {
      append_field(line, "content_digest", entry.content_digest.to_hex());
    }
    if (entry.sequence.value() != 0) {
      append_field(line, "sequence", to_decimal(entry.sequence.value()));
    }
    if (entry.subject_scope.has_value()) {
      append_field(line, "subject_scope", entry.subject_scope->view());
    }
    if (entry.subject_equipment.has_value()) {
      append_field(line, "subject_equipment", entry.subject_equipment->view());
    }
    if (!entry.reference.empty()) {
      append_field(line, "reference", entry.reference.view());
    }
    if (!entry.label.empty()) {
      append_field(line, "label", quote_text(entry.label.view()));
    }
    if (!entry.notes.empty()) {
      append_field(line, "notes", quote_text(entry.notes.view()));
    }
    text += line;
    text += "\n";
  }

  std::vector<IndependenceDomain> domains = input.independence_domains;
  sort_records(domains, [](const IndependenceDomain& value) { return value.id; });
  for (const IndependenceDomain& domain : domains) {
    line = "DOMAIN";
    append_field(line, "id", domain.id.view());
    append_field(line, "scope", domain.scope.view());
    if (!domain.evidence.empty()) {
      append_field(line, "evidence", domain.evidence.view());
    }
    if (!domain.label.empty()) {
      append_field(line, "label", quote_text(domain.label.view()));
    }
    append_field(line, "epoch", to_decimal(domain.epoch.value()));
    append_field(line, "topology", to_decimal(domain.topology_generation.value()));
    text += line;
    text += "\n";
  }

  std::vector<AccountingScope> scopes = input.scopes;
  sort_records(scopes, [](const AccountingScope& value) { return value.id; });
  for (const AccountingScope& scope : scopes) {
    line = "SCOPE";
    append_field(line, "id", scope.id.view());
    append_field(line, "kind", scope_kind_name(scope.kind));
    append_field(line, "medium", medium_name(scope.medium));
    if (scope.parent.has_value()) {
      append_field(line, "parent", scope.parent->view());
    }
    if (!scope.label.empty()) {
      append_field(line, "label", quote_text(scope.label.view()));
    }
    if (!scope.classes.empty()) {
      std::string joined;
      for (const EquipmentClassId& klass : scope.classes) {
        if (!joined.empty()) {
          joined.push_back(',');
        }
        joined += klass.str();
      }
      append_field(line, "classes", joined);
    }
    if (scope.declared_installed_total.is_known()) {
      append_field(line, "declared_mw",
                   to_decimal(scope.declared_installed_total.value().milliwatts()));
    } else if (scope.declared_installed_total.is_unknown()) {
      append_field(line, "declared_unknown",
                   measure_reason_name(scope.declared_installed_total.reason()));
    }
    if (!scope.declared_total_evidence.empty()) {
      std::string joined;
      for (const EvidenceId& evidence_id : scope.declared_total_evidence) {
        if (!joined.empty()) {
          joined.push_back(',');
        }
        joined += evidence_id.str();
      }
      append_field(line, "declared_evidence", joined);
    }
    append_field(line, "epoch", to_decimal(scope.epoch.value()));
    append_field(line, "topology", to_decimal(scope.topology_generation.value()));
    text += line;
    text += "\n";
  }

  std::vector<ContributionGroup> groups = input.groups;
  sort_records(groups, [](const ContributionGroup& value) { return value.id; });
  for (const ContributionGroup& group : groups) {
    line = "GROUP";
    append_field(line, "id", group.id.view());
    append_field(line, "scope", group.scope.view());
    append_field(line, "classification", contribution_class_name(group.classification));
    append_field(line, "medium", medium_name(group.medium));
    append_field(line, "required",
                 to_decimal(static_cast<std::uint64_t>(group.required_concurrent)));
    append_field(line, "redundancy", redundancy_class_name(group.redundancy));
    if (group.protected_quantity.is_known()) {
      append_field(line, "protected_mw",
                   to_decimal(group.protected_quantity.value().milliwatts()));
    } else if (group.protected_quantity.is_unknown()) {
      append_field(line, "protected_unknown",
                   measure_reason_name(group.protected_quantity.reason()));
    } else {
      append_field(line, "protected_unsupported",
                   measure_reason_name(group.protected_quantity.reason()));
    }
    if (!group.protected_quantity_evidence.empty()) {
      append_field(line, "protected_evidence", group.protected_quantity_evidence.view());
    }
    if (!group.independence_domains.empty()) {
      std::string joined;
      for (const IndependenceDomainId& domain : group.independence_domains) {
        if (!joined.empty()) {
          joined.push_back(',');
        }
        joined += domain.str();
      }
      append_field(line, "domains", joined);
    }
    append_field(line, "epoch", to_decimal(group.binding.epoch.value()));
    append_field(line, "topology", to_decimal(group.binding.topology.value()));
    append_field(line, "policy", to_decimal(group.binding.policy.value()));
    append_field(line, "evidence_generation", to_decimal(group.binding.evidence.value()));
    text += line;
    text += "\n";
  }

  std::vector<Contribution> contributions = input.contributions;
  sort_records(contributions, [](const Contribution& value) { return value.id; });
  for (Contribution& contribution : contributions) {
    sort_records(contribution.derates,
                 [](const DerateFactor& value) { return value.id; });
    sort_records(contribution.sharing.shares,
                 [](const ApportionmentShare& value) { return value.target; });
    line = "CONTRIBUTION";
    append_field(line, "id", contribution.id.view());
    append_field(line, "scope", contribution.home_scope.view());
    append_field(line, "equipment", contribution.equipment.view());
    append_field(line, "class", contribution.equipment_class.view());
    append_field(line, "medium", medium_name(contribution.medium));
    append_field(line, "classification",
                 contribution_class_name(contribution.classification));
    append_field(line, "service", service_state_name(contribution.service));
    if (contribution.installed.is_known()) {
      append_field(line, "installed_mw",
                   to_decimal(contribution.installed.value().milliwatts()));
    } else if (contribution.installed.is_unknown()) {
      append_field(line, "installed_unknown",
                   measure_reason_name(contribution.installed.reason()));
    } else {
      append_field(line, "installed_unsupported",
                   measure_reason_name(contribution.installed.reason()));
    }
    append_field(line, "priority",
                 to_decimal(static_cast<std::uint64_t>(contribution.priority)));
    if (contribution.group.has_value()) {
      append_field(line, "group", contribution.group->view());
    }
    if (contribution.independence_domain.has_value()) {
      append_field(line, "domain", contribution.independence_domain->view());
    }
    if (!contribution.aliases.empty()) {
      std::string joined;
      for (const ScopeId& alias : contribution.aliases) {
        if (!joined.empty()) {
          joined.push_back(',');
        }
        joined += alias.str();
      }
      append_field(line, "aliases", joined);
    }
    append_field(line, "epoch", to_decimal(contribution.binding.epoch.value()));
    append_field(line, "topology", to_decimal(contribution.binding.topology.value()));
    append_field(line, "policy", to_decimal(contribution.binding.policy.value()));
    append_field(line, "evidence_generation",
                 to_decimal(contribution.binding.evidence.value()));
    if (!contribution.primary_evidence.empty()) {
      append_field(line, "evidence", contribution.primary_evidence.view());
    }
    if (!contribution.evidence.empty()) {
      std::string joined;
      for (const EvidenceId& reference : contribution.evidence) {
        if (!joined.empty()) {
          joined.push_back(',');
        }
        joined += reference.str();
      }
      append_field(line, "evidence_refs", joined);
    }
    append_field(line, "observed_at", format_utc(contribution.observed_at));
    if (contribution.observation_sequence.value() != 0) {
      append_field(line, "sequence",
                   to_decimal(contribution.observation_sequence.value()));
    }
    text += line;
    text += "\n";

    for (const DerateFactor& derate : contribution.derates) {
      line = "DERATE";
      append_field(line, "contribution", contribution.id.view());
      append_field(line, "id", derate.id.view());
      append_field(line, "kind", derate_kind_name(derate.kind));
      if (derate.kind == DerateKind::Factor) {
        append_field(line, "ppm",
                     to_decimal(static_cast<std::uint64_t>(derate.factor.ppm())));
      } else {
        append_field(line, "mw", to_decimal(derate.absolute.milliwatts()));
      }
      if (!derate.evidence.empty()) {
        append_field(line, "evidence", derate.evidence.view());
      }
      if (!derate.label.empty()) {
        append_field(line, "label", quote_text(derate.label.view()));
      }
      text += line;
      text += "\n";
    }
    for (const ApportionmentShare& share : contribution.sharing.shares) {
      line = "SHARE";
      append_field(line, "contribution", contribution.id.view());
      append_field(line, "target", share.target.view());
      append_field(line, "ppm",
                   to_decimal(static_cast<std::uint64_t>(share.share.ppm())));
      text += line;
      text += "\n";
    }
  }

  std::vector<ManifestDeclaration> manifests = input.manifest_declarations;
  sort_records(manifests, [](const ManifestDeclaration& value) { return value.id; });
  for (const ManifestDeclaration& declaration : manifests) {
    line = "MANIFEST";
    append_field(line, "id", declaration.id.view());
    append_field(line, "scope", declaration.scope.view());
    append_field(line, "medium", medium_name(declaration.medium));
    append_field(line, "declared_mw",
                 to_decimal(declaration.declared_installed.milliwatts()));
    append_field(line, "evidence", declaration.evidence.view());
    if (declaration.equipment_class.has_value()) {
      append_field(line, "class", declaration.equipment_class->view());
    }
    append_field(line, "observed_at", format_utc(declaration.observed_at));
    append_field(line, "epoch", to_decimal(declaration.binding.epoch.value()));
    append_field(line, "topology", to_decimal(declaration.binding.topology.value()));
    append_field(line, "policy", to_decimal(declaration.binding.policy.value()));
    append_field(line, "evidence_generation",
                 to_decimal(declaration.binding.evidence.value()));
    text += line;
    text += "\n";
  }

  text += "END\n";
  return text;
}

}  // namespace cooling_capacity_accounting
