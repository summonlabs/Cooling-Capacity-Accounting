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

// The accounting policy: its own validation, the bounds it may not raise, the
// digest that identifies it, its rendering, and the three agreements it must
// reach with the generation being accounted. Every fixture is SYNTHETIC.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

namespace {

using namespace cooling_capacity_accounting;
using cca_test::id_of;

AccountingInput facility() {
  cca_test::FacilitySpec spec;
  spec.zones = 1;
  spec.loops_per_zone = 1;
  spec.units_per_loop = 2;
  spec.unit_installed_mw = 250000;
  return cca_test::make_facility(spec);
}

bool contains(std::string_view text, std::string_view needle) {
  return text.find(needle) != std::string_view::npos;
}

// ---------------------------------------------------------------------------

CCA_TEST(policy_baseline_is_valid) {
  const AccountingPolicy baseline = AccountingPolicy::baseline();
  CCA_CHECK(baseline.validate(Limits{}).ok());
  CCA_CHECK(baseline.validate(Limits::defaults()).ok());
  CCA_CHECK(!baseline.id.empty());
  CCA_CHECK(baseline.generation.is_initial());
  CCA_CHECK(baseline.epoch.is_initial());
  CCA_CHECK_EQ(baseline.max_derate_factors, kMaxDerateFactors);
  CCA_CHECK_EQ(baseline.retention_generations, kMaxRetainedGenerations);
  CCA_CHECK(!baseline.max_evidence_age.is_zero());
  CCA_CHECK(baseline.minimum_coverage.is_one());
  CCA_CHECK(baseline.residual_tolerance.is_none());

  // Building the same policy twice yields the same identity and the same
  // digest, and so does an unmodified copy.
  const AccountingPolicy again = AccountingPolicy::baseline();
  CCA_CHECK_EQ(again.digest(), baseline.digest());
  AccountingPolicy copy = baseline;
  CCA_CHECK_EQ(copy.digest(), baseline.digest());
  CCA_CHECK_EQ(copy.to_string(), baseline.to_string());
}

CCA_TEST(policy_validate_refuses_an_impossible_configuration) {
  const AccountingPolicy baseline = AccountingPolicy::baseline();
  const Limits defaults = Limits::defaults();

  // The derate bound may not be zero and may not exceed the hard limit.
  AccountingPolicy none = baseline;
  none.max_derate_factors = 0;
  CCA_CHECK_CODE(none.validate(defaults), ErrorCode::DerateOutOfRange);
  AccountingPolicy raised = baseline;
  raised.max_derate_factors = kMaxDerateFactors + 1;
  CCA_CHECK_CODE(raised.validate(defaults), ErrorCode::DerateOutOfRange);
  AccountingPolicy one = baseline;
  one.max_derate_factors = 1;
  CCA_CHECK(one.validate(defaults).ok());
  AccountingPolicy maximum = baseline;
  maximum.max_derate_factors = kMaxDerateFactors;
  CCA_CHECK(maximum.validate(defaults).ok());

  // Retention may not be zero, and may not exceed the configured bound.
  AccountingPolicy no_retention = baseline;
  no_retention.retention_generations = 0;
  CCA_CHECK_CODE(no_retention.validate(defaults), ErrorCode::OutOfRange);
  AccountingPolicy above_limits = baseline;
  above_limits.retention_generations = defaults.max_retained_generations + 1;
  CCA_CHECK_CODE(above_limits.validate(defaults), ErrorCode::LimitExceeded);
  AccountingPolicy lowered = baseline;
  lowered.retention_generations = defaults.max_retained_generations;
  CCA_CHECK(lowered.validate(defaults).ok());

  // The policy must be identifiable.
  AccountingPolicy anonymous = baseline;
  anonymous.id = PolicyId();
  CCA_CHECK_CODE(anonymous.validate(defaults), ErrorCode::MissingRequiredField);

  // An effective instant in the future is not a validation failure of the
  // policy itself: it is refused when it meets a generation.
  AccountingPolicy later = baseline;
  later.effective_from = cca_test::fixture_now();
  CCA_CHECK(later.validate(defaults).ok());
}

CCA_TEST(policy_limits_may_only_be_lowered) {
  CCA_CHECK(Limits::defaults().validate().ok());

  Limits zeroed;
  zeroed.max_scopes = 0;
  CCA_CHECK_CODE(zeroed.validate(), ErrorCode::InvalidArgument);
  Limits zeroed_findings;
  zeroed_findings.max_findings = 0;
  CCA_CHECK_CODE(zeroed_findings.validate(), ErrorCode::InvalidArgument);

  Limits raised;
  raised.max_scopes = kMaxScopes + 1;
  CCA_CHECK_CODE(raised.validate(), ErrorCode::LimitExceeded);
  Limits raised_depth;
  raised_depth.max_scope_nesting_depth = kMaxScopeNestingDepth + 1;
  CCA_CHECK_CODE(raised_depth.validate(), ErrorCode::LimitExceeded);

  Limits lowered;
  lowered.max_retained_generations = 4;
  CCA_CHECK(lowered.validate().ok());

  // A policy may use at most what the limits allow; the limits are checked
  // first, so a bad limit is the primary failure.
  AccountingPolicy policy = AccountingPolicy::baseline();
  policy.retention_generations = 5;
  CCA_CHECK_CODE(policy.validate(lowered), ErrorCode::LimitExceeded);
  policy.retention_generations = 4;
  CCA_CHECK(policy.validate(lowered).ok());

  Limits bad_limits;
  bad_limits.max_retained_generations = kMaxRetainedGenerations + 1;
  policy.retention_generations = 3;
  CCA_CHECK_CODE(policy.validate(bad_limits), ErrorCode::LimitExceeded);

  // The same precedence is visible through the public accounting entry points.
  AccountingInput input = facility();
  input.policy.retention_generations = 5;
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now(), lowered),
                 ErrorCode::LimitExceeded);
  CCA_CHECK_CODE(AccountingLedger::validate_input(input, cca_test::fixture_now(), lowered),
                 ErrorCode::LimitExceeded);
}

CCA_TEST(policy_digest_changes_with_every_field) {
  const AccountingPolicy baseline = AccountingPolicy::baseline();
  const Digest reference = baseline.digest();
  CCA_CHECK(!reference.is_zero());

  AccountingPolicy modified = baseline;
  modified.id = id_of<PolicyId>("cca.policy.other");
  CCA_CHECK(modified.digest() != reference);

  modified = baseline;
  modified.generation = PolicyGeneration::of(7).value();
  CCA_CHECK(modified.digest() != reference);

  modified = baseline;
  modified.epoch = ControlPlaneEpoch::of(3).value();
  CCA_CHECK(modified.digest() != reference);

  modified = baseline;
  modified.effective_from = Timestamp::of_unix_milliseconds(1);
  CCA_CHECK(modified.digest() != reference);

  modified = baseline;
  modified.max_evidence_age = DurationMs::of_milliseconds(1);
  CCA_CHECK(modified.digest() != reference);

  modified = baseline;
  modified.require_out_of_service_evidence = !baseline.require_out_of_service_evidence;
  CCA_CHECK(modified.digest() != reference);

  modified = baseline;
  modified.require_classification = !baseline.require_classification;
  CCA_CHECK(modified.digest() != reference);

  modified = baseline;
  modified.require_installed_evidence = !baseline.require_installed_evidence;
  CCA_CHECK(modified.digest() != reference);

  modified = baseline;
  modified.require_derate_evidence = !baseline.require_derate_evidence;
  CCA_CHECK(modified.digest() != reference);

  modified = baseline;
  modified.minimum_coverage = Ratio::of_ppm(500000).value();
  CCA_CHECK(modified.digest() != reference);

  modified = baseline;
  modified.residual_tolerance = Ratio::of_ppm(1).value();
  CCA_CHECK(modified.digest() != reference);

  modified = baseline;
  modified.max_derate_factors = 1;
  CCA_CHECK(modified.digest() != reference);

  modified = baseline;
  modified.retention_generations = 1;
  CCA_CHECK(modified.digest() != reference);

  modified = baseline;
  modified.note = BoundedText::from_validated("synthetic policy note");
  CCA_CHECK(modified.digest() != reference);

  // A field restored to its baseline value restores the digest exactly.
  modified = baseline;
  modified.note = BoundedText::from_validated("synthetic policy note");
  modified.note = BoundedText();
  CCA_CHECK_EQ(modified.digest(), reference);
}

CCA_TEST(policy_to_string_names_its_identity) {
  const AccountingPolicy baseline = AccountingPolicy::baseline();
  const std::string text = baseline.to_string();
  CCA_CHECK(contains(text, baseline.id.str()));
  CCA_CHECK(contains(text, "generation="));
  CCA_CHECK(contains(text, "epoch="));
  CCA_CHECK(contains(text, "max_evidence_age="));
  CCA_CHECK(contains(text, "retention="));

  AccountingPolicy other = baseline;
  other.id = id_of<PolicyId>("cca.policy.other");
  other.generation = PolicyGeneration::of(7).value();
  const std::string other_text = other.to_string();
  CCA_CHECK(contains(other_text, "cca.policy.other"));
  CCA_CHECK(contains(other_text, "generation=7"));
  CCA_CHECK(contains(other_text, "epoch=1"));
}

CCA_TEST(policy_generation_must_match_the_accounting_generation) {
  AccountingInput input = facility();
  CCA_CHECK_EQ(input.policy.generation, input.generations.policy);
  CCA_CHECK(AccountingLedger::build(input, cca_test::fixture_now()).ok());

  input.policy.generation = PolicyGeneration::of(input.generations.policy.value() + 1U).value();
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::PolicyGenerationMismatch);
  CCA_CHECK_CODE(AccountingLedger::validate_input(input, cca_test::fixture_now()),
                 ErrorCode::PolicyGenerationMismatch);

  // The same policy reaches the same generation when both move together.
  AccountingInput moved = facility();
  moved.generations.policy = PolicyGeneration::of(9).value();
  moved.policy.generation = moved.generations.policy;
  CCA_CHECK(AccountingLedger::build(moved, cca_test::fixture_now()).ok());
}

CCA_TEST(policy_epoch_must_match_the_accounting_epoch) {
  AccountingInput input = facility();
  input.policy.epoch = ControlPlaneEpoch::of(input.generations.epoch.value() + 1U).value();
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::CrossEpochAuthority);
  CCA_CHECK_CODE(AccountingLedger::validate_input(input, cca_test::fixture_now()),
                 ErrorCode::CrossEpochAuthority);

  AccountingInput moved = facility();
  moved.generations.epoch = ControlPlaneEpoch::of(4).value();
  moved.policy.epoch = moved.generations.epoch;
  CCA_CHECK(AccountingLedger::build(moved, cca_test::fixture_now()).ok());
}

CCA_TEST(policy_must_be_effective_at_the_accounting_instant) {
  const Timestamp now = cca_test::fixture_now();
  AccountingInput input = facility();
  input.policy.effective_from = Timestamp::of_unix_milliseconds(now.unix_milliseconds() + 1);
  CCA_CHECK_CODE(AccountingLedger::build(input, now), ErrorCode::FutureGeneration);
  CCA_CHECK_CODE(AccountingLedger::validate_input(input, now), ErrorCode::FutureGeneration);

  // Effective exactly at the accounting instant is effective.
  input.policy.effective_from = now;
  CCA_CHECK(AccountingLedger::build(input, now).ok());

  // And so is a policy that took effect at the epoch.
  input.policy.effective_from = Timestamp::epoch();
  CCA_CHECK(AccountingLedger::build(input, now).ok());
}

}  // namespace
