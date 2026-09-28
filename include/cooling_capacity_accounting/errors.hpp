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

#ifndef COOLING_CAPACITY_ACCOUNTING_ERRORS_HPP
#define COOLING_CAPACITY_ACCOUNTING_ERRORS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace cooling_capacity_accounting {

/// Machine-readable failure codes.
///
/// The numeric values are part of the stable public contract: they may be added
/// to, but an existing code never changes value or meaning.
enum class ErrorCode : std::int32_t {
  Ok = 0,

  // -- usage -------------------------------------------------------------
  InvalidArgument = 100,
  UnexpectedState = 101,
  NotSupported = 102,

  // -- input -------------------------------------------------------------
  InvalidIdentifier = 200,
  InvalidText = 201,
  InvalidUtf8 = 202,
  InvalidNumber = 203,
  UnknownKeyword = 204,
  MissingRequiredField = 205,
  DuplicateField = 206,
  /// A compile-time format bound on a count or a length was exceeded.
  TooManyItems = 207,
  ItemTooLarge = 208,
  TruncatedInput = 209,
  TrailingBytes = 210,
  MalformedRecord = 211,
  UnknownRecordKind = 212,
  ImpossibleEnumValue = 213,
  ReservedFieldNotZero = 214,
  InvalidDigestText = 215,
  InvalidTimestampText = 216,
  RecordOrderViolation = 217,
  FutureTimestamp = 218,

  // -- structure ---------------------------------------------------------
  DuplicateIdentity = 300,
  DuplicateScope = 301,
  DuplicateEvidence = 302,
  DuplicateContribution = 303,
  DuplicateIndependenceDomain = 304,
  UnknownScope = 305,
  UnknownReference = 306,
  CyclicScopeNesting = 307,
  ScopeNestingTooDeep = 308,
  OrphanScope = 309,
  ScopeKindMismatch = 310,
  MissingScopeKind = 311,
  AmbiguousHomeScope = 312,
  MutuallyExclusiveGroupConflict = 313,
  ContributionGroupMissing = 314,
  GroupRequiresExclusiveSharing = 315,
  ShareTargetsHomeScope = 316,
  DuplicateShareTarget = 317,
  GroupScopeMismatch = 318,
  ContributionOutsideClassBand = 319,

  // -- reference / generation -------------------------------------------
  StaleGeneration = 400,
  FutureGeneration = 401,
  SupersededGeneration = 402,
  StaleEpoch = 403,
  CrossEpochAuthority = 404,
  GenerationMismatch = 405,
  StaleEvidence = 406,
  StalePolicy = 407,
  TopologyGenerationMismatch = 408,
  PolicyGenerationMismatch = 409,
  EvidenceGenerationMismatch = 410,
  StateRevisionMismatch = 411,
  PublishPreconditionFailed = 412,
  SupersededWriter = 413,
  RecoveredStateRequiresRevalidation = 414,

  // -- quantity ----------------------------------------------------------
  NumericOverflow = 500,
  NumericUnderflow = 501,
  DivisionByZero = 502,
  OutOfRange = 503,
  NegativeQuantity = 504,
  ClosureViolation = 505,
  DerateOutOfRange = 506,
  WrongUnit = 507,

  // -- policy / accounting semantics ------------------------------------
  UnknownMeasurement = 600,
  UnsupportedCapability = 601,
  MediumMismatch = 602,
  ClassMismatch = 603,
  IndependenceNotDeclared = 604,
  MissingIndependenceEvidence = 605,
  ReserveShortfall = 606,
  OverApportioned = 607,
  ApportionmentIncomplete = 608,
  RequiredEvidenceMissing = 609,
  PolicyViolation = 610,
  OutOfServiceWithoutEvidence = 611,
  ClassificationRequired = 612,
  ConflictingContribution = 613,
  UnresolvedResidual = 614,
  ReserveUnresolved = 615,
  NameplateMismatch = 616,
  CoverageBelowMinimum = 617,
  NonContributingClass = 618,
  AliasOfUnknownContribution = 619,

  // -- persistence -------------------------------------------------------
  StoreNotFound = 700,
  StoreInUse = 701,
  WriterLockUnavailable = 702,
  IntegrityFailure = 703,
  DigestMismatch = 704,
  UnsupportedFormatVersion = 705,
  NoPublishedGeneration = 706,
  PublishSequenceRegressed = 707,
  IdempotencyConflict = 708,
  GenerationNotRetained = 709,
  PartialPublication = 710,
  StoreCorrupt = 711,
  AttemptMismatch = 712,

  // -- resource ----------------------------------------------------------
  /// A bound the caller configured through Limits was exceeded.
  LimitExceeded = 800,
  AllocationFailed = 801,

  // -- environment -------------------------------------------------------
  IoFailure = 900,
  PathInvalid = 901,
  PathTooLong = 902,
  PathTraversalRejected = 903,
  ReparsePointRejected = 904,
  PermissionDenied = 905,
  FlushFailed = 906,
  LockFailure = 907,
  ProcessFailure = 908,

  // -- lifecycle ---------------------------------------------------------
  ShuttingDown = 950,
  Cancelled = 951,

  // -- internal ----------------------------------------------------------
  InternalError = 999,
};

/// Broad class of a failure. The category is also the validation stage that
/// produced the code, so a caller can reason about precedence.
enum class ErrorCategory : std::int32_t {
  Ok = 0,
  Usage = 1,
  Input = 2,
  Structure = 3,
  Reference = 4,
  Quantity = 5,
  Policy = 6,
  Persistence = 7,
  Resource = 8,
  Environment = 9,
  Lifecycle = 10,
  Internal = 11,
};

/// Short, stable name of an error code ("InvalidIdentifier", ...).
[[nodiscard]] std::string_view error_code_name(ErrorCode code) noexcept;
/// Short, stable name of an error category.
[[nodiscard]] std::string_view error_category_name(ErrorCategory category) noexcept;
/// Category a code belongs to.
[[nodiscard]] ErrorCategory category_of(ErrorCode code) noexcept;
/// Message text a code carries when no further explanation is supplied.
[[nodiscard]] std::string_view default_message(ErrorCode code) noexcept;
/// True when the caller can retry the identical request and may succeed.
[[nodiscard]] bool is_retryable(ErrorCode code) noexcept;

/// A failure value. The machine contract is the code; the text is for humans.
class Error {
 public:
  Error() = default;

  static Error of(ErrorCode code);
  static Error of(ErrorCode code, std::string message);

  [[nodiscard]] ErrorCode code() const noexcept { return code_; }
  [[nodiscard]] ErrorCategory category() const noexcept { return category_; }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
  /// Subject the failure is about: an identifier, a field name or a path.
  [[nodiscard]] const std::string& subject() const noexcept { return subject_; }
  /// Additional machine-adjacent context (an index, an observed value).
  [[nodiscard]] const std::string& detail() const noexcept { return detail_; }
  [[nodiscard]] bool ok() const noexcept { return code_ == ErrorCode::Ok; }

  Error& with_subject(std::string subject);
  Error& with_detail(std::string detail);
  Error& with_message(std::string message);

  /// "Code: message (subject: x) (detail: y)".
  [[nodiscard]] std::string to_string() const;

  friend bool operator==(const Error& lhs, const Error& rhs) noexcept {
    return lhs.code_ == rhs.code_ && lhs.subject_ == rhs.subject_ &&
           lhs.detail_ == rhs.detail_;
  }
  friend bool operator!=(const Error& lhs, const Error& rhs) noexcept {
    return !(lhs == rhs);
  }

 private:
  ErrorCode code_ = ErrorCode::Ok;
  ErrorCategory category_ = ErrorCategory::Ok;
  std::string message_;
  std::string subject_;
  std::string detail_;
};

/// Success marker for Result<void>.
struct Ok {};

/// Either a value or an Error. The public API never uses exceptions as its
/// machine contract.
template <typename T>
class Result {
 public:
  Result(T value) : value_(std::move(value)), ok_(true) {}
  Result(Error error) : error_(std::move(error)), ok_(false) {}

  [[nodiscard]] bool ok() const noexcept { return ok_; }
  explicit operator bool() const noexcept { return ok_; }

  /// Precondition: ok(). Never throws; a violated precondition is a caller bug.
  [[nodiscard]] const T& value() const noexcept { return *value_; }
  [[nodiscard]] T& value() noexcept { return *value_; }
  [[nodiscard]] T take() noexcept { return std::move(*value_); }

  [[nodiscard]] const Error& error() const noexcept { return error_; }
  [[nodiscard]] ErrorCode code() const noexcept { return error_.code(); }

  /// Value when successful, otherwise the supplied fallback.
  [[nodiscard]] T value_or(T fallback) const {
    return ok_ ? *value_ : std::move(fallback);
  }

 private:
  std::optional<T> value_;
  Error error_{};
  bool ok_;
};

template <>
class Result<void> {
 public:
  Result(Ok) : error_(), ok_(true) {}
  Result(Error error) : error_(std::move(error)), ok_(false) {}

  [[nodiscard]] bool ok() const noexcept { return ok_; }
  explicit operator bool() const noexcept { return ok_; }
  [[nodiscard]] const Error& error() const noexcept { return error_; }
  [[nodiscard]] ErrorCode code() const noexcept { return error_.code(); }

 private:
  Error error_{};
  bool ok_;
};

/// Propagates a failed Result as the caller's return value.
#define CCA_TRY(expr)                        \
  do {                                       \
    const auto cca_try_result_ = (expr);     \
    if (!cca_try_result_.ok()) {             \
      return cca_try_result_.error();        \
    }                                        \
  } while (false)

/// Binds the value of a successful Result to a new name, or returns its error.
#define CCA_TRY_ASSIGN(name, expr)                  \
  auto cca_try_value_##name = (expr);               \
  if (!cca_try_value_##name.ok()) {                 \
    return cca_try_value_##name.error();            \
  }                                                 \
  auto name = cca_try_value_##name.take()

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_ERRORS_HPP
