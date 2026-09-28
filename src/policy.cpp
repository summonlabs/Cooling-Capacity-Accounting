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

#include "cooling_capacity_accounting/policy.hpp"

#include <string>

#include "cooling_capacity_accounting/clock.hpp"
#include "cooling_capacity_accounting/digest.hpp"

namespace cooling_capacity_accounting {
namespace {

constexpr std::int64_t kBaselineFreshnessMilliseconds = 86'400'000;  // 24 hours

}  // namespace

AccountingPolicy AccountingPolicy::baseline() {
  AccountingPolicy policy;
  policy.id = PolicyId::from_validated(Identifier::from_validated("cca.policy.baseline"));
  policy.generation = PolicyGeneration::initial();
  policy.epoch = ControlPlaneEpoch::initial();
  policy.effective_from = Timestamp::epoch();
  policy.max_evidence_age = DurationMs::of_milliseconds(kBaselineFreshnessMilliseconds);
  policy.minimum_coverage = Ratio::one();
  policy.residual_tolerance = Ratio::none();
  policy.max_derate_factors = kMaxDerateFactors;
  policy.retention_generations = kMaxRetainedGenerations;
  return policy;
}

Result<void> AccountingPolicy::validate(const Limits& limits) const {
  CCA_TRY(limits.validate());
  if (id.empty()) {
    return Error::of(ErrorCode::MissingRequiredField, "the policy must be identified")
        .with_subject("policy.id");
  }
  if (max_derate_factors == 0 || max_derate_factors > kMaxDerateFactors) {
    return Error::of(ErrorCode::DerateOutOfRange,
                     "max_derate_factors must be between 1 and the hard limit")
        .with_detail("max_derate_factors " +
                     to_decimal(static_cast<std::uint64_t>(max_derate_factors)) +
                     " exceeds " +
                     to_decimal(static_cast<std::uint64_t>(kMaxDerateFactors)));
  }
  if (retention_generations == 0) {
    return Error::of(ErrorCode::OutOfRange,
                     "retention_generations must be at least one");
  }
  if (retention_generations > limits.max_retained_generations) {
    return Error::of(ErrorCode::LimitExceeded,
                     "retention_generations exceeds the configured limit")
        .with_detail("retention_generations " +
                     to_decimal(static_cast<std::uint64_t>(retention_generations)) +
                     " exceeds " +
                     to_decimal(static_cast<std::uint64_t>(
                         limits.max_retained_generations)));
  }
  if (minimum_coverage.ppm() > Ratio::scale()) {
    return Error::of(ErrorCode::OutOfRange, "minimum_coverage is above 1.0");
  }
  return Ok{};
}

Digest AccountingPolicy::digest() const {
  DigestBuilder builder;
  builder.add_section("policy");
  builder.add_field("id", id.view());
  builder.add_field_u64("generation", generation.value());
  builder.add_field_u64("epoch", epoch.value());
  builder.add_field_i64("effective_from", effective_from.unix_milliseconds());
  builder.add_field_i64("max_evidence_age_ms", max_evidence_age.milliseconds());
  builder.add_field_u64("require_out_of_service_evidence",
                        require_out_of_service_evidence ? 1U : 0U);
  builder.add_field_u64("require_classification", require_classification ? 1U : 0U);
  builder.add_field_u64("require_installed_evidence",
                        require_installed_evidence ? 1U : 0U);
  builder.add_field_u64("require_derate_evidence", require_derate_evidence ? 1U : 0U);
  builder.add_field_u64("minimum_coverage_ppm", minimum_coverage.ppm());
  builder.add_field_u64("residual_tolerance_ppm", residual_tolerance.ppm());
  builder.add_field_u64("max_derate_factors",
                        static_cast<std::uint64_t>(max_derate_factors));
  builder.add_field_u64("retention_generations",
                        static_cast<std::uint64_t>(retention_generations));
  builder.add_field("note", note.view());
  return builder.finish();
}

std::string AccountingPolicy::to_string() const {
  std::string text = "policy ";
  text += id.str();
  text += " generation=";
  text += to_decimal(generation.value());
  text += " epoch=";
  text += to_decimal(epoch.value());
  text += " max_evidence_age=";
  text += format_duration(max_evidence_age);
  text += " minimum_coverage_ppm=";
  text += to_decimal(static_cast<std::uint64_t>(minimum_coverage.ppm()));
  text += " residual_tolerance_ppm=";
  text += to_decimal(static_cast<std::uint64_t>(residual_tolerance.ppm()));
  text += " retention=";
  text += to_decimal(static_cast<std::uint64_t>(retention_generations));
  return text;
}

}  // namespace cooling_capacity_accounting
