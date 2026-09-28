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

// Proof obligations for the configurable bounds: the defaults are exactly the
// compile-time bounds, every bound may only be lowered (never raised, never
// zeroed), and a payload that declares more records than the bound allows is
// refused from its declared count alone, before any record is read.

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

namespace {

using cooling_capacity_accounting::AccountingInput;
using cooling_capacity_accounting::AccountingLedger;
using cooling_capacity_accounting::decode_canonical;
using cooling_capacity_accounting::encode_canonical;
using cooling_capacity_accounting::ErrorCode;
using cooling_capacity_accounting::GenerationBundle;
using cooling_capacity_accounting::kMaxAttemptRecords;
using cooling_capacity_accounting::kMaxContributions;
using cooling_capacity_accounting::kMaxEvidenceRecords;
using cooling_capacity_accounting::kMaxFindings;
using cooling_capacity_accounting::kMaxIndependenceDomains;
using cooling_capacity_accounting::kMaxRetainedGenerations;
using cooling_capacity_accounting::kMaxScopeNestingDepth;
using cooling_capacity_accounting::kMaxScopes;
using cooling_capacity_accounting::Limits;
using cooling_capacity_accounting::Result;
using cooling_capacity_accounting::Timestamp;

/// A synthetic facility: zones + 1 zones, loops_per_zone + 1 loops and one
/// contribution per loop.
[[nodiscard]] AccountingInput small_facility(std::size_t zones,
                                             std::size_t loops_per_zone) {
  cca_test::FacilitySpec spec;
  spec.zones = zones;
  spec.loops_per_zone = loops_per_zone;
  spec.units_per_loop = 1U;
  return cca_test::make_facility(spec);
}

[[nodiscard]] std::uint32_t big_endian_u32(const std::vector<std::uint8_t>& bytes,
                                           std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4U; ++index) {
    value = (value << 8U) | static_cast<std::uint32_t>(bytes[offset + index]);
  }
  return value;
}

}  // namespace

CCA_TEST(default_limits_are_exactly_the_compile_time_bounds) {
  const Limits defaults = Limits::defaults();
  CCA_CHECK_EQ(defaults.max_scopes, kMaxScopes);
  CCA_CHECK_EQ(defaults.max_contributions, kMaxContributions);
  CCA_CHECK_EQ(defaults.max_evidence_records, kMaxEvidenceRecords);
  CCA_CHECK_EQ(defaults.max_independence_domains, kMaxIndependenceDomains);
  CCA_CHECK_EQ(defaults.max_scope_nesting_depth, kMaxScopeNestingDepth);
  CCA_CHECK_EQ(defaults.max_findings, kMaxFindings);
  CCA_CHECK_EQ(defaults.max_retained_generations, kMaxRetainedGenerations);
  CCA_CHECK_EQ(defaults.max_attempt_records, kMaxAttemptRecords);

  CCA_CHECK(Limits::defaults().validate().ok());
  CCA_CHECK(Limits{}.validate().ok());
  CCA_CHECK_EQ(Limits{}.max_scopes, kMaxScopes);

  // Every bound is meaningful: none of the defaults is zero.
  CCA_CHECK(defaults.max_scopes > 0U);
  CCA_CHECK(defaults.max_contributions > 0U);
  CCA_CHECK(defaults.max_evidence_records > 0U);
  CCA_CHECK(defaults.max_independence_domains > 0U);
  CCA_CHECK(defaults.max_scope_nesting_depth > 0U);
  CCA_CHECK(defaults.max_findings > 0U);
  CCA_CHECK(defaults.max_retained_generations > 0U);
  CCA_CHECK(defaults.max_attempt_records > 0U);
}

CCA_TEST(every_limit_may_only_be_lowered_never_raised_or_zeroed) {
  std::size_t Limits::*const fields[] = {
      &Limits::max_scopes,
      &Limits::max_contributions,
      &Limits::max_evidence_records,
      &Limits::max_independence_domains,
      &Limits::max_scope_nesting_depth,
      &Limits::max_findings,
      &Limits::max_retained_generations,
      &Limits::max_attempt_records,
  };
  const std::size_t ceilings[] = {
      kMaxScopes,          kMaxContributions,       kMaxEvidenceRecords,
      kMaxIndependenceDomains, kMaxScopeNestingDepth, kMaxFindings,
      kMaxRetainedGenerations, kMaxAttemptRecords,
  };
  const char* const names[] = {
      "max_scopes",           "max_contributions",
      "max_evidence_records", "max_independence_domains",
      "max_scope_nesting_depth", "max_findings",
      "max_retained_generations", "max_attempt_records",
  };
  CCA_CHECK_EQ(std::size(fields), std::size(ceilings));
  CCA_CHECK_EQ(std::size(fields), std::size(names));

  for (std::size_t index = 0; index < std::size(fields); ++index) {
    // The exact ceiling is accepted.
    Limits at_ceiling = Limits::defaults();
    at_ceiling.*fields[index] = ceilings[index];
    if (!at_ceiling.validate().ok()) {
      CCA_FAIL(std::string("the ceiling of ") + names[index] + " must be accepted");
    }
    // Lowering is accepted, down to one.
    Limits lowered = Limits::defaults();
    lowered.*fields[index] = 1U;
    if (!lowered.validate().ok()) {
      CCA_FAIL(std::string("lowering ") + names[index] + " to one must be accepted");
    }
    // One past the ceiling is refused as a limit violation.
    Limits raised = Limits::defaults();
    raised.*fields[index] = ceilings[index] + 1U;
    const Result<void> raised_result = raised.validate();
    if (raised_result.ok()) {
      CCA_FAIL(std::string("raising ") + names[index] + " must be refused");
    } else if (raised_result.error().code() != ErrorCode::LimitExceeded) {
      CCA_FAIL(std::string("raising ") + names[index] +
               " must report LimitExceeded, observed " +
               std::string(cooling_capacity_accounting::error_code_name(
                   raised_result.error().code())));
    }
    // Zero makes the bound meaningless and is refused as an argument error.
    Limits zeroed = Limits::defaults();
    zeroed.*fields[index] = 0U;
    const Result<void> zeroed_result = zeroed.validate();
    if (zeroed_result.ok()) {
      CCA_FAIL(std::string("zeroing ") + names[index] + " must be refused");
    } else if (zeroed_result.error().code() != ErrorCode::InvalidArgument) {
      CCA_FAIL(std::string("zeroing ") + names[index] +
               " must report InvalidArgument, observed " +
               std::string(cooling_capacity_accounting::error_code_name(
                   zeroed_result.error().code())));
    }
  }
}

CCA_TEST(accounting_refuses_too_many_scopes_at_the_boundary) {
  const AccountingInput input = small_facility(2U, 2U);
  const Timestamp at = cca_test::fixture_now();
  // One site plus two zones plus four loops.
  CCA_CHECK_EQ(input.scopes.size(), 7U);
  CCA_CHECK_EQ(input.contributions.size(), 4U);

  Limits exact = Limits::defaults();
  exact.max_scopes = 7U;
  CCA_CHECK(AccountingLedger::validate_input(input, at, exact).ok());
  CCA_CHECK(AccountingLedger::build(input, at, exact).ok());

  Limits one_past = Limits::defaults();
  one_past.max_scopes = 6U;
  CCA_CHECK_CODE(AccountingLedger::validate_input(input, at, one_past),
                 ErrorCode::LimitExceeded);
  CCA_CHECK_CODE(AccountingLedger::build(input, at, one_past),
                 ErrorCode::LimitExceeded);

  // The default bound accepts the same input, so only the bound changed.
  CCA_CHECK(AccountingLedger::validate_input(input, at).ok());
}

CCA_TEST(accounting_refuses_too_many_contributions_at_the_boundary) {
  const AccountingInput input = small_facility(2U, 2U);
  const Timestamp at = cca_test::fixture_now();
  CCA_CHECK_EQ(input.contributions.size(), 4U);

  Limits exact = Limits::defaults();
  exact.max_contributions = 4U;
  CCA_CHECK(AccountingLedger::build(input, at, exact).ok());

  Limits one_past = Limits::defaults();
  one_past.max_contributions = 3U;
  CCA_CHECK_CODE(AccountingLedger::build(input, at, one_past),
                 ErrorCode::LimitExceeded);
  CCA_CHECK_CODE(AccountingLedger::validate_input(input, at, one_past),
                 ErrorCode::LimitExceeded);
}

CCA_TEST(decoding_accepts_the_exact_bound_and_refuses_one_past) {
  const AccountingInput input = small_facility(2U, 2U);
  CCA_ASSIGN(encoded, encode_canonical(input));

  Limits exact = Limits::defaults();
  exact.max_scopes = 7U;
  exact.max_contributions = 4U;
  CCA_ASSIGN(decoded, decode_canonical(encoded, exact));
  CCA_CHECK_EQ(decoded.scopes.size(), 7U);
  CCA_CHECK_EQ(decoded.contributions.size(), 4U);

  // Re-encoding the decoded input reproduces the original bytes exactly.
  CCA_ASSIGN(reencoded, encode_canonical(decoded, exact));
  CCA_CHECK(reencoded == encoded);

  Limits short_contributions = exact;
  short_contributions.max_contributions = 3U;
  CCA_CHECK_CODE(decode_canonical(encoded, short_contributions),
                 ErrorCode::LimitExceeded);

  Limits short_scopes = exact;
  short_scopes.max_scopes = 6U;
  CCA_CHECK_CODE(decode_canonical(encoded, short_scopes), ErrorCode::LimitExceeded);

  // The default bounds accept the same payload.
  CCA_CHECK(decode_canonical(encoded).ok());
}

CCA_TEST(a_declared_record_count_beyond_the_bound_is_refused_before_any_record) {
  // A policy-only input declares zero records of every kind, so its seven record
  // counts are contiguous directly before the trailer and each one can be
  // verified before it is patched. A record count sits next to its records, so
  // the counts of a populated payload are not contiguous and cannot be located
  // this way.
  AccountingInput input;
  input.generations = GenerationBundle::initial();
  input.policy = cca_test::relaxed_policy();
  CCA_ASSIGN(encoded, encode_canonical(input));
  CCA_CHECK(encoded.size() > 36U);

  const std::size_t counts_offset = encoded.size() - 36U;
  CCA_CHECK_EQ(std::string_view(
                   reinterpret_cast<const char*>(encoded.data() + counts_offset + 28U),
                   8U),
               std::string_view("CCAEND01"));
  for (std::size_t index = 0; index < 7U; ++index) {
    CCA_CHECK_EQ(big_endian_u32(encoded, counts_offset + index * 4U), 0U);
  }

  // The unpatched payload decodes.
  CCA_CHECK(decode_canonical(encoded).ok());

  // A payload that claims 4294967295 contributions while containing none is
  // refused from its declared count alone: no record is read and nothing is
  // allocated for the declared count.
  std::vector<std::uint8_t> oversized = encoded;
  const std::size_t contribution_field = counts_offset + 5U * 4U;
  for (std::size_t byte = 0; byte < 4U; ++byte) {
    oversized[contribution_field + byte] = 0xFFU;
  }
  const Result<AccountingInput> refused = decode_canonical(oversized);
  CCA_CHECK(!refused.ok());
  CCA_CHECK_EQ(refused.error().code(), ErrorCode::LimitExceeded);
  CCA_CHECK(!refused.error().detail().empty());

  // The same declared count is refused for equipment classes, scopes and
  // manifest declarations.
  for (const std::size_t field :
       {counts_offset + 0U, counts_offset + 12U, counts_offset + 24U}) {
    std::vector<std::uint8_t> patched = encoded;
    for (std::size_t byte = 0; byte < 4U; ++byte) {
      patched[field + byte] = 0xFFU;
    }
    CCA_CHECK_CODE(decode_canonical(patched), ErrorCode::LimitExceeded);
  }

  // A lowered bound that still covers the declared counts accepts the payload,
  // so the refusals above are the bounds and not the patch.
  Limits exact = Limits::defaults();
  exact.max_scopes = 1U;
  exact.max_contributions = 1U;
  CCA_CHECK(decode_canonical(encoded, exact).ok());
}
