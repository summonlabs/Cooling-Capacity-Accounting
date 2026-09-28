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

#include "cooling_capacity_accounting/limits.hpp"

#include "cooling_capacity_accounting/text.hpp"

namespace cooling_capacity_accounting {
namespace {

[[nodiscard]] Result<void> require_at_most(std::size_t value, std::size_t ceiling,
                                           const char* name) {
  if (value == 0) {
    return Error::of(ErrorCode::InvalidArgument, std::string(name) +
                                                     " must be greater than zero");
  }
  if (value > ceiling) {
    return Error::of(ErrorCode::LimitExceeded,
                     std::string(name) + " may only be lowered, never raised")
        .with_detail(std::string(name) + " " + to_decimal(static_cast<std::uint64_t>(value)) +
                     " exceeds the hard bound " +
                     to_decimal(static_cast<std::uint64_t>(ceiling)));
  }
  return Ok{};
}

}  // namespace

Result<void> Limits::validate() const {
  CCA_TRY(require_at_most(max_scopes, kMaxScopes, "max_scopes"));
  CCA_TRY(require_at_most(max_contributions, kMaxContributions, "max_contributions"));
  CCA_TRY(require_at_most(max_evidence_records, kMaxEvidenceRecords,
                          "max_evidence_records"));
  CCA_TRY(require_at_most(max_independence_domains, kMaxIndependenceDomains,
                          "max_independence_domains"));
  CCA_TRY(require_at_most(max_scope_nesting_depth, kMaxScopeNestingDepth,
                          "max_scope_nesting_depth"));
  CCA_TRY(require_at_most(max_findings, kMaxFindings, "max_findings"));
  CCA_TRY(require_at_most(max_retained_generations, kMaxRetainedGenerations,
                          "max_retained_generations"));
  CCA_TRY(require_at_most(max_attempt_records, kMaxAttemptRecords,
                          "max_attempt_records"));
  return Ok{};
}

}  // namespace cooling_capacity_accounting
