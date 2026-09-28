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

#ifndef COOLING_CAPACITY_ACCOUNTING_LIMITS_HPP
#define COOLING_CAPACITY_ACCOUNTING_LIMITS_HPP

#include <cstddef>
#include <cstdint>

#include "cooling_capacity_accounting/errors.hpp"

namespace cooling_capacity_accounting {

/// Hard upper bounds. Every bound is checked before the allocation it bounds,
/// so no untrusted count is ever used directly as an allocation size.
inline constexpr std::size_t kMaxIdentifierLength = 64;
inline constexpr std::size_t kMaxTextLength = 512;
inline constexpr std::size_t kMaxReferenceLength = 256;
inline constexpr std::size_t kMaxPathLength = 200;

inline constexpr std::size_t kMaxScopes = 8192;
inline constexpr std::size_t kMaxContributions = 65536;
inline constexpr std::size_t kMaxEvidenceRecords = 131072;
inline constexpr std::size_t kMaxIndependenceDomains = 4096;
inline constexpr std::size_t kMaxManifestDeclarations = 8192;
inline constexpr std::size_t kMaxFindings = 16384;

inline constexpr std::size_t kMaxScopeNestingDepth = 8;
inline constexpr std::size_t kMaxAliasesPerContribution = 64;
inline constexpr std::size_t kMaxApportionTargets = 64;
inline constexpr std::size_t kMaxEvidencePerContribution = 32;
inline constexpr std::size_t kMaxDerateFactors = 3;
inline constexpr std::size_t kMaxGroupMembers = 256;
inline constexpr std::size_t kMaxInterchangeLines = 262144;
inline constexpr std::size_t kMaxLineLength = 8192;

inline constexpr std::size_t kMaxCanonicalRecordBytes = 1U << 20;
inline constexpr std::size_t kMaxCanonicalPayloadBytes = 64U << 20;
inline constexpr std::size_t kMaxStoreGenerations = 4096;
inline constexpr std::size_t kMaxRetainedGenerations = 64;
inline constexpr std::size_t kMaxHistoryEntries = 4096;
inline constexpr std::size_t kMaxAttemptRecords = 1024;

/// Highest thermal power the accounting domain accepts, in milliwatts
/// (1e15 mW = 1 TW). Values above this are refused rather than accumulated.
inline constexpr std::int64_t kMaxThermalPowerMilliwatts = 1'000'000'000'000'000LL;

/// Highest redundancy multiplier the ladder can express.
inline constexpr std::uint32_t kMaxRedundancyCopies = 2;
/// Highest number of concurrent declared-domain failures the ladder expresses.
inline constexpr std::uint32_t kMaxRedundancyFailures = 2;

/// Configurable subset of the bounds. The compile-time constants above are the
/// defaults; a caller may only lower them.
struct Limits {
  std::size_t max_scopes = kMaxScopes;
  std::size_t max_contributions = kMaxContributions;
  std::size_t max_evidence_records = kMaxEvidenceRecords;
  std::size_t max_independence_domains = kMaxIndependenceDomains;
  std::size_t max_scope_nesting_depth = kMaxScopeNestingDepth;
  std::size_t max_findings = kMaxFindings;
  std::size_t max_retained_generations = kMaxRetainedGenerations;
  std::size_t max_attempt_records = kMaxAttemptRecords;

  /// Refuses a configuration that raises a compile-time bound or zeroes one
  /// out in a way that makes the accounting meaningless.
  [[nodiscard]] Result<void> validate() const;

  [[nodiscard]] static Limits defaults() noexcept { return Limits{}; }
};

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_LIMITS_HPP
