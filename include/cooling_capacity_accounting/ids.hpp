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

#ifndef COOLING_CAPACITY_ACCOUNTING_IDS_HPP
#define COOLING_CAPACITY_ACCOUNTING_IDS_HPP

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "cooling_capacity_accounting/digest.hpp"
#include "cooling_capacity_accounting/errors.hpp"
#include "cooling_capacity_accounting/text.hpp"

namespace cooling_capacity_accounting {

/// Tag types. Each tag is a distinct C++ type, so two identifiers drawn from
/// different domains can never be interchanged or compared by accident.
struct SiteTag {};
struct ZoneTag {};
struct LoopTag {};
struct EquipmentTag {};
struct EquipmentClassTag {};
struct ScopeTag {};
struct ContributionTag {};
struct ContributionGroupTag {};
struct IndependenceDomainTag {};
struct EvidenceTag {};
struct EvidenceSourceTag {};
struct DerateTag {};
struct PolicyTag {};
struct ManifestTag {};

/// A validated identifier carrying the identity domain of its tag.
template <typename Tag>
class StrongIdentifier {
 public:
  using tag_type = Tag;

  StrongIdentifier() = default;

  [[nodiscard]] static Result<StrongIdentifier> parse(std::string_view text) {
    CCA_TRY_ASSIGN(parsed, Identifier::parse(text));
    StrongIdentifier result;
    result.value_ = std::move(parsed);
    return result;
  }

  [[nodiscard]] static StrongIdentifier from_validated(Identifier id) noexcept {
    StrongIdentifier result;
    result.value_ = std::move(id);
    return result;
  }

  [[nodiscard]] const Identifier& value() const noexcept { return value_; }
  [[nodiscard]] const std::string& str() const noexcept { return value_.str(); }
  [[nodiscard]] std::string_view view() const noexcept { return value_.view(); }
  [[nodiscard]] bool empty() const noexcept { return value_.empty(); }

  friend bool operator==(const StrongIdentifier& lhs,
                         const StrongIdentifier& rhs) noexcept {
    return lhs.value_ == rhs.value_;
  }
  friend bool operator!=(const StrongIdentifier& lhs,
                         const StrongIdentifier& rhs) noexcept {
    return !(lhs == rhs);
  }
  friend bool operator<(const StrongIdentifier& lhs,
                        const StrongIdentifier& rhs) noexcept {
    return lhs.value_ < rhs.value_;
  }

 private:
  Identifier value_;
};

using SiteId = StrongIdentifier<SiteTag>;
using ZoneId = StrongIdentifier<ZoneTag>;
using LoopId = StrongIdentifier<LoopTag>;
using EquipmentId = StrongIdentifier<EquipmentTag>;
using EquipmentClassId = StrongIdentifier<EquipmentClassTag>;
using ScopeId = StrongIdentifier<ScopeTag>;
using ContributionId = StrongIdentifier<ContributionTag>;
using ContributionGroupId = StrongIdentifier<ContributionGroupTag>;
using IndependenceDomainId = StrongIdentifier<IndependenceDomainTag>;
using EvidenceId = StrongIdentifier<EvidenceTag>;
using EvidenceSourceId = StrongIdentifier<EvidenceSourceTag>;
using DerateId = StrongIdentifier<DerateTag>;
using PolicyId = StrongIdentifier<PolicyTag>;
using ManifestId = StrongIdentifier<ManifestTag>;

/// A monotonically increasing counter whose *meaning* is fixed by its tag.
/// Generations, epochs, revisions and sequences are deliberately different
/// types: a topology generation is not a state revision.
template <typename Tag>
class Counter {
 public:
  using tag_type = Tag;
  using value_type = std::uint64_t;

  Counter() = default;

  [[nodiscard]] static constexpr Counter initial() noexcept {
    return Counter(Tag::kInitial);
  }
  [[nodiscard]] static Result<Counter> of(std::uint64_t value) {
    if (value < Tag::kInitial) {
      return Error::of(ErrorCode::OutOfRange, std::string(Tag::kName))
          .with_detail("counter value " + to_decimal(value) +
                       " precedes the initial value " +
                       to_decimal(static_cast<std::uint64_t>(Tag::kInitial)));
    }
    return Counter(value);
  }
  [[nodiscard]] static Result<Counter> parse(std::string_view text) {
    CCA_TRY_ASSIGN(value, parse_uint64(text));
    return of(value);
  }

  [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_initial() const noexcept {
    return value_ == Tag::kInitial;
  }
  /// The next counter value, refusing to wrap around.
  [[nodiscard]] Result<Counter> next() const {
    if (value_ == std::numeric_limits<std::uint64_t>::max()) {
      return Error::of(ErrorCode::NumericOverflow, std::string(Tag::kName));
    }
    return Counter(value_ + 1U);
  }

  friend constexpr bool operator==(Counter lhs, Counter rhs) noexcept {
    return lhs.value_ == rhs.value_;
  }
  friend constexpr bool operator!=(Counter lhs, Counter rhs) noexcept {
    return lhs.value_ != rhs.value_;
  }
  friend constexpr bool operator<(Counter lhs, Counter rhs) noexcept {
    return lhs.value_ < rhs.value_;
  }
  friend constexpr bool operator<=(Counter lhs, Counter rhs) noexcept {
    return lhs.value_ <= rhs.value_;
  }
  friend constexpr bool operator>(Counter lhs, Counter rhs) noexcept {
    return lhs.value_ > rhs.value_;
  }
  friend constexpr bool operator>=(Counter lhs, Counter rhs) noexcept {
    return lhs.value_ >= rhs.value_;
  }

 private:
  constexpr explicit Counter(std::uint64_t value) noexcept : value_(value) {}
  std::uint64_t value_ = Tag::kInitial;
};

struct ControlPlaneEpochTag {
  static constexpr std::uint64_t kInitial = 1;
  static constexpr std::string_view kName = "control plane epoch";
};
struct TopologyGenerationTag {
  static constexpr std::uint64_t kInitial = 1;
  static constexpr std::string_view kName = "topology generation";
};
struct PolicyGenerationTag {
  static constexpr std::uint64_t kInitial = 1;
  static constexpr std::string_view kName = "policy generation";
};
struct EvidenceGenerationTag {
  static constexpr std::uint64_t kInitial = 1;
  static constexpr std::string_view kName = "evidence generation";
};
struct AccountGenerationTag {
  static constexpr std::uint64_t kInitial = 1;
  static constexpr std::string_view kName = "accounting generation";
};
struct StateRevisionTag {
  static constexpr std::uint64_t kInitial = 0;
  static constexpr std::string_view kName = "state revision";
};
struct CommitSequenceTag {
  static constexpr std::uint64_t kInitial = 0;
  static constexpr std::string_view kName = "commit sequence";
};
struct ObservationSequenceTag {
  static constexpr std::uint64_t kInitial = 0;
  static constexpr std::string_view kName = "observation sequence";
};

using ControlPlaneEpoch = Counter<ControlPlaneEpochTag>;
using TopologyGeneration = Counter<TopologyGenerationTag>;
using PolicyGeneration = Counter<PolicyGenerationTag>;
using EvidenceGeneration = Counter<EvidenceGenerationTag>;
using AccountGeneration = Counter<AccountGenerationTag>;
using StateRevision = Counter<StateRevisionTag>;
using CommitSequence = Counter<CommitSequenceTag>;
using ObservationSequence = Counter<ObservationSequenceTag>;

/// A 128-bit identity used for attempt and incarnation identifiers. It is
/// derived from the request content, so an identical retry is recognisable.
class WideId {
 public:
  WideId() = default;

  [[nodiscard]] static WideId from_digest(const Digest& digest) noexcept;
  [[nodiscard]] static Result<WideId> parse(std::string_view hex32);
  [[nodiscard]] static Result<WideId> from_material(std::string_view material);

  [[nodiscard]] constexpr std::uint64_t high() const noexcept { return high_; }
  [[nodiscard]] constexpr std::uint64_t low() const noexcept { return low_; }
  [[nodiscard]] std::string to_string() const;

  friend constexpr bool operator==(WideId lhs, WideId rhs) noexcept {
    return lhs.high_ == rhs.high_ && lhs.low_ == rhs.low_;
  }
  friend constexpr bool operator!=(WideId lhs, WideId rhs) noexcept {
    return !(lhs == rhs);
  }
  friend constexpr bool operator<(WideId lhs, WideId rhs) noexcept {
    return lhs.high_ != rhs.high_ ? lhs.high_ < rhs.high_ : lhs.low_ < rhs.low_;
  }

 private:
  std::uint64_t high_ = 0;
  std::uint64_t low_ = 0;
};

/// Identifies one attempt at a mutation. The same attempt identifier replayed
/// with the same fingerprint is a retry; with a different fingerprint it is a
/// conflict.
using AttemptId = WideId;
/// Identifies one incarnation of a writer process. A restarted runtime is a
/// different incarnation even when it reopens the same store.
using IncarnationId = WideId;

/// The complete state a request was planned against: epoch, evidence,
/// topology, policy and revision. Every authority-bearing call carries one.
struct GenerationBundle {
  ControlPlaneEpoch epoch;
  TopologyGeneration topology;
  PolicyGeneration policy;
  EvidenceGeneration evidence;
  StateRevision revision;

  [[nodiscard]] static GenerationBundle initial() noexcept {
    return GenerationBundle{ControlPlaneEpoch::initial(), TopologyGeneration::initial(),
                            PolicyGeneration::initial(), EvidenceGeneration::initial(),
                            StateRevision::initial()};
  }

  friend bool operator==(const GenerationBundle& lhs,
                         const GenerationBundle& rhs) noexcept {
    return lhs.epoch == rhs.epoch && lhs.topology == rhs.topology &&
           lhs.policy == rhs.policy && lhs.evidence == rhs.evidence &&
           lhs.revision == rhs.revision;
  }
  friend bool operator!=(const GenerationBundle& lhs,
                         const GenerationBundle& rhs) noexcept {
    return !(lhs == rhs);
  }
};

/// Result of comparing two generation bundles.
enum class GenerationOrder : std::int32_t {
  Equal = 0,
  Older = 1,
  Newer = 2,
  Incomparable = 3,
};

/// Compares two generation bundles. Bundles from different control-plane epochs
/// are Incomparable rather than ordered; within one epoch the comparison is
/// lexicographic by (topology, policy, evidence, revision).
[[nodiscard]] GenerationOrder compare_generations(const GenerationBundle& lhs,
                                                  const GenerationBundle& rhs) noexcept;
[[nodiscard]] std::string_view generation_order_name(GenerationOrder order) noexcept;

/// Which persisted writer fence a process currently holds.
struct WriterFence {
  IncarnationId incarnation;
  ControlPlaneEpoch epoch;
  CommitSequence last_commit;

  friend bool operator==(const WriterFence& lhs, const WriterFence& rhs) noexcept {
    return lhs.incarnation == rhs.incarnation && lhs.epoch == rhs.epoch &&
           lhs.last_commit == rhs.last_commit;
  }
};

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_IDS_HPP
