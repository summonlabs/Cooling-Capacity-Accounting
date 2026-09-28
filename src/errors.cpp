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

#include "cooling_capacity_accounting/errors.hpp"

#include <string>
#include <string_view>

namespace cooling_capacity_accounting {
namespace {

struct ErrorInfo {
  ErrorCode code;
  ErrorCategory category;
  std::string_view name;
  std::string_view message;
  bool retryable;
};

// Every code the library can return, with its stable name, category and
// default message. A unit test walks this table so a code can never be added
// without its contract.
constexpr ErrorInfo kErrorTable[] = {
    {ErrorCode::Ok, ErrorCategory::Ok, "Ok", "success", false},

    {ErrorCode::InvalidArgument, ErrorCategory::Usage, "InvalidArgument",
     "the call used an argument that is not valid here", false},
    {ErrorCode::UnexpectedState, ErrorCategory::Usage, "UnexpectedState",
     "the object is not in a state where this call is meaningful", false},
    {ErrorCode::NotSupported, ErrorCategory::Usage, "NotSupported",
     "this build does not support the requested operation", false},

    {ErrorCode::InvalidIdentifier, ErrorCategory::Input, "InvalidIdentifier",
     "the identifier is not well formed", false},
    {ErrorCode::InvalidText, ErrorCategory::Input, "InvalidText",
     "the text is not acceptable for this field", false},
    {ErrorCode::InvalidUtf8, ErrorCategory::Input, "InvalidUtf8",
     "the text is not valid UTF-8", false},
    {ErrorCode::InvalidNumber, ErrorCategory::Input, "InvalidNumber",
     "the number is not a canonical decimal", false},
    {ErrorCode::UnknownKeyword, ErrorCategory::Input, "UnknownKeyword",
     "the record contains a keyword this format does not define", false},
    {ErrorCode::MissingRequiredField, ErrorCategory::Input, "MissingRequiredField",
     "a required field is absent", false},
    {ErrorCode::DuplicateField, ErrorCategory::Input, "DuplicateField",
     "a field that may appear once appeared more than once", false},
    {ErrorCode::TooManyItems, ErrorCategory::Input, "TooManyItems",
     "the input contains more items than the bound allows", false},
    {ErrorCode::ItemTooLarge, ErrorCategory::Input, "ItemTooLarge",
     "an item is larger than the bound allows", false},
    {ErrorCode::TruncatedInput, ErrorCategory::Input, "TruncatedInput",
     "the input ends in the middle of a record", false},
    {ErrorCode::TrailingBytes, ErrorCategory::Input, "TrailingBytes",
     "the input continues after its final record", false},
    {ErrorCode::MalformedRecord, ErrorCategory::Input, "MalformedRecord",
     "the record is not in the documented form", false},
    {ErrorCode::UnknownRecordKind, ErrorCategory::Input, "UnknownRecordKind",
     "the record names a kind this format does not define", false},
    {ErrorCode::ImpossibleEnumValue, ErrorCategory::Input, "ImpossibleEnumValue",
     "an enumerated field holds a value outside its definition", false},
    {ErrorCode::ReservedFieldNotZero, ErrorCategory::Input, "ReservedFieldNotZero",
     "a reserved field is not zero", false},
    {ErrorCode::InvalidDigestText, ErrorCategory::Input, "InvalidDigestText",
     "the digest is not 64 hexadecimal characters", false},
    {ErrorCode::InvalidTimestampText, ErrorCategory::Input, "InvalidTimestampText",
     "the instant is not in the canonical UTC form", false},
    {ErrorCode::RecordOrderViolation, ErrorCategory::Input, "RecordOrderViolation",
     "records appear in an order the format does not allow", false},
    {ErrorCode::FutureTimestamp, ErrorCategory::Input, "FutureTimestamp",
     "the observation is later than the instant it is accounted at", false},

    {ErrorCode::DuplicateIdentity, ErrorCategory::Structure, "DuplicateIdentity",
     "two records in the same identity domain share an identifier", false},
    {ErrorCode::DuplicateScope, ErrorCategory::Structure, "DuplicateScope",
     "two scopes share an identifier", false},
    {ErrorCode::DuplicateEvidence, ErrorCategory::Structure, "DuplicateEvidence",
     "two evidence records share an identifier", false},
    {ErrorCode::DuplicateContribution, ErrorCategory::Structure, "DuplicateContribution",
     "two contributions share an identifier", false},
    {ErrorCode::DuplicateIndependenceDomain, ErrorCategory::Structure,
     "DuplicateIndependenceDomain",
     "two shared-fate declarations share an identifier", false},
    {ErrorCode::UnknownScope, ErrorCategory::Structure, "UnknownScope",
     "the record names a scope that does not exist", false},
    {ErrorCode::UnknownReference, ErrorCategory::Structure, "UnknownReference",
     "the record names something that does not exist", false},
    {ErrorCode::CyclicScopeNesting, ErrorCategory::Structure, "CyclicScopeNesting",
     "scope nesting contains a cycle", false},
    {ErrorCode::ScopeNestingTooDeep, ErrorCategory::Structure, "ScopeNestingTooDeep",
     "scope nesting is deeper than the bound allows", false},
    {ErrorCode::OrphanScope, ErrorCategory::Structure, "OrphanScope",
     "a scope has no path to a root scope", false},
    {ErrorCode::ScopeKindMismatch, ErrorCategory::Structure, "ScopeKindMismatch",
     "a scope of this kind may not nest under that kind", false},
    {ErrorCode::MissingScopeKind, ErrorCategory::Structure, "MissingScopeKind",
     "a scope does not declare its kind", false},
    {ErrorCode::AmbiguousHomeScope, ErrorCategory::Structure, "AmbiguousHomeScope",
     "the contribution's home scope, class and medium do not agree", false},
    {ErrorCode::MutuallyExclusiveGroupConflict, ErrorCategory::Structure,
     "MutuallyExclusiveGroupConflict",
     "a mutually exclusive group has no member that can be selected", false},
    {ErrorCode::ContributionGroupMissing, ErrorCategory::Structure,
     "ContributionGroupMissing",
     "the contribution class requires a group and none is named", false},
    {ErrorCode::GroupRequiresExclusiveSharing, ErrorCategory::Structure,
     "GroupRequiresExclusiveSharing",
     "a grouped contribution may not be apportioned across scopes", false},
    {ErrorCode::ShareTargetsHomeScope, ErrorCategory::Structure, "ShareTargetsHomeScope",
     "an apportionment share targets the contribution's own home scope", false},
    {ErrorCode::DuplicateShareTarget, ErrorCategory::Structure, "DuplicateShareTarget",
     "two apportionment shares target the same scope", false},
    {ErrorCode::GroupScopeMismatch, ErrorCategory::Structure, "GroupScopeMismatch",
     "a group member's home scope is not the group's scope", false},
    {ErrorCode::ContributionOutsideClassBand, ErrorCategory::Structure,
     "ContributionOutsideClassBand",
     "a contribution's class is not among the classes its scope declares", false},

    {ErrorCode::StaleGeneration, ErrorCategory::Reference, "StaleGeneration",
     "the request was planned against a generation that has been superseded", false},
    {ErrorCode::FutureGeneration, ErrorCategory::Reference, "FutureGeneration",
     "the request names a generation the authority has not reached", false},
    {ErrorCode::SupersededGeneration, ErrorCategory::Reference, "SupersededGeneration",
     "the generation has been replaced by a later one", false},
    {ErrorCode::StaleEpoch, ErrorCategory::Reference, "StaleEpoch",
     "the writer holds a control-plane epoch older than the recorded one", false},
    {ErrorCode::CrossEpochAuthority, ErrorCategory::Reference, "CrossEpochAuthority",
     "the records mix control-plane epochs", false},
    {ErrorCode::GenerationMismatch, ErrorCategory::Reference, "GenerationMismatch",
     "the generations of the request and the authority do not agree", false},
    {ErrorCode::StaleEvidence, ErrorCategory::Reference, "StaleEvidence",
     "the evidence is older than the freshness window allows", false},
    {ErrorCode::StalePolicy, ErrorCategory::Reference, "StalePolicy",
     "the policy generation is not the current one", false},
    {ErrorCode::TopologyGenerationMismatch, ErrorCategory::Reference,
     "TopologyGenerationMismatch",
     "the topology generation of the record is not the current one", false},
    {ErrorCode::PolicyGenerationMismatch, ErrorCategory::Reference,
     "PolicyGenerationMismatch",
     "the policy generation of the record is not the current one", false},
    {ErrorCode::EvidenceGenerationMismatch, ErrorCategory::Reference,
     "EvidenceGenerationMismatch",
     "the evidence generation of the record is not the current one", false},
    {ErrorCode::StateRevisionMismatch, ErrorCategory::Reference, "StateRevisionMismatch",
     "the state revision the request was planned against is no longer current",
     true},
    {ErrorCode::PublishPreconditionFailed, ErrorCategory::Reference,
     "PublishPreconditionFailed",
     "the publish precondition does not hold for the current generation", true},
    {ErrorCode::SupersededWriter, ErrorCategory::Reference, "SupersededWriter",
     "a later writer incarnation has already committed to this store", false},
    {ErrorCode::RecoveredStateRequiresRevalidation, ErrorCategory::Reference,
     "RecoveredStateRequiresRevalidation",
     "the state was recovered from the store and has not been revalidated", false},

    {ErrorCode::NumericOverflow, ErrorCategory::Quantity, "NumericOverflow",
     "exact arithmetic would exceed the representable range", false},
    {ErrorCode::NumericUnderflow, ErrorCategory::Quantity, "NumericUnderflow",
     "exact arithmetic would go below zero", false},
    {ErrorCode::DivisionByZero, ErrorCategory::Quantity, "DivisionByZero",
     "the divisor is zero", false},
    {ErrorCode::OutOfRange, ErrorCategory::Quantity, "OutOfRange",
     "the quantity is outside the range this domain accepts", false},
    {ErrorCode::NegativeQuantity, ErrorCategory::Quantity, "NegativeQuantity",
     "a quantity that may not be negative is negative", false},
    {ErrorCode::ClosureViolation, ErrorCategory::Quantity, "ClosureViolation",
     "the accounting identity does not hold for the scope", false},
    {ErrorCode::DerateOutOfRange, ErrorCategory::Quantity, "DerateOutOfRange",
     "the derate is outside the range a derate may take", false},
    {ErrorCode::WrongUnit, ErrorCategory::Quantity, "WrongUnit",
     "the quantity is expressed in a unit this position does not accept", false},

    {ErrorCode::UnknownMeasurement, ErrorCategory::Policy, "UnknownMeasurement",
     "the quantity is unknown and nothing may be concluded from it", false},
    {ErrorCode::UnsupportedCapability, ErrorCategory::Policy, "UnsupportedCapability",
     "the capability does not exist in this configuration", false},
    {ErrorCode::MediumMismatch, ErrorCategory::Policy, "MediumMismatch",
     "the record's medium does not match its scope or class", false},
    {ErrorCode::ClassMismatch, ErrorCategory::Policy, "ClassMismatch",
     "the equipment class does not exist in this scope", false},
    {ErrorCode::IndependenceNotDeclared, ErrorCategory::Policy, "IndependenceNotDeclared",
     "an in-service member of a redundant group declares no shared-fate boundary",
     false},
    {ErrorCode::MissingIndependenceEvidence, ErrorCategory::Policy,
     "MissingIndependenceEvidence",
     "a shared-fate declaration carries no evidence for the declaration", false},
    {ErrorCode::ReserveShortfall, ErrorCategory::Policy, "ReserveShortfall",
     "the installed capacity is below what the redundancy class requires", false},
    {ErrorCode::OverApportioned, ErrorCategory::Policy, "OverApportioned",
     "the apportionment shares total more than the whole contribution", false},
    {ErrorCode::ApportionmentIncomplete, ErrorCategory::Policy, "ApportionmentIncomplete",
     "an apportioned contribution leaves part of its quantity unapportioned", false},
    {ErrorCode::RequiredEvidenceMissing, ErrorCategory::Policy, "RequiredEvidenceMissing",
     "the policy requires evidence that the record does not name", false},
    {ErrorCode::PolicyViolation, ErrorCategory::Policy, "PolicyViolation",
     "the record is not acceptable under the accounting policy", false},
    {ErrorCode::OutOfServiceWithoutEvidence, ErrorCategory::Policy,
     "OutOfServiceWithoutEvidence",
     "an out-of-service declaration names no evidence", false},
    {ErrorCode::ClassificationRequired, ErrorCategory::Policy, "ClassificationRequired",
     "the policy requires every contribution to state its contribution class",
     false},
    {ErrorCode::ConflictingContribution, ErrorCategory::Policy, "ConflictingContribution",
     "two records claim the same identity with different content", false},
    {ErrorCode::UnresolvedResidual, ErrorCategory::Policy, "UnresolvedResidual",
     "an unexplained residual remains in the accounting", false},
    {ErrorCode::ReserveUnresolved, ErrorCategory::Policy, "ReserveUnresolved",
     "the reserve obligation cannot be established from the declared evidence",
     false},
    {ErrorCode::NameplateMismatch, ErrorCategory::Policy, "NameplateMismatch",
     "the accounted quantity disagrees with the evidence it cites", false},
    {ErrorCode::CoverageBelowMinimum, ErrorCategory::Policy, "CoverageBelowMinimum",
     "the determinate share of the declared mass is below the policy minimum",
     false},
    {ErrorCode::NonContributingClass, ErrorCategory::Policy, "NonContributingClass",
     "the equipment class is declared as not contributing to installed capacity",
     false},
    {ErrorCode::AliasOfUnknownContribution, ErrorCategory::Policy,
     "AliasOfUnknownContribution",
     "an alias names a contribution that does not exist", false},

    {ErrorCode::StoreNotFound, ErrorCategory::Persistence, "StoreNotFound",
     "the store directory or manifest does not exist", false},
    {ErrorCode::StoreInUse, ErrorCategory::Persistence, "StoreInUse",
     "another process holds the writer lock", true},
    {ErrorCode::WriterLockUnavailable, ErrorCategory::Persistence,
     "WriterLockUnavailable",
     "the writer lock could not be acquired", true},
    {ErrorCode::IntegrityFailure, ErrorCategory::Persistence, "IntegrityFailure",
     "the stored bytes do not match their recorded digest", false},
    {ErrorCode::DigestMismatch, ErrorCategory::Persistence, "DigestMismatch",
     "the digest of the content does not match the digest that was recorded",
     false},
    {ErrorCode::UnsupportedFormatVersion, ErrorCategory::Persistence,
     "UnsupportedFormatVersion",
     "the stored format version is not supported", false},
    {ErrorCode::NoPublishedGeneration, ErrorCategory::Persistence,
     "NoPublishedGeneration",
     "the store holds no published generation", false},
    {ErrorCode::PublishSequenceRegressed, ErrorCategory::Persistence,
     "PublishSequenceRegressed",
     "the publish would move the commit sequence backwards", false},
    {ErrorCode::IdempotencyConflict, ErrorCategory::Persistence, "IdempotencyConflict",
     "the attempt identifier was reused for a different request", false},
    {ErrorCode::GenerationNotRetained, ErrorCategory::Persistence,
     "GenerationNotRetained",
     "the generation has been retired by retention", false},
    {ErrorCode::PartialPublication, ErrorCategory::Persistence, "PartialPublication",
     "a publication was interrupted before its commit point", false},
    {ErrorCode::StoreCorrupt, ErrorCategory::Persistence, "StoreCorrupt",
     "the store's authoritative state cannot be established", false},
    {ErrorCode::AttemptMismatch, ErrorCategory::Persistence, "AttemptMismatch",
     "the recorded attempt does not describe this request", false},

    {ErrorCode::LimitExceeded, ErrorCategory::Resource, "LimitExceeded",
     "the operation would exceed a configured bound", false},
    {ErrorCode::AllocationFailed, ErrorCategory::Resource, "AllocationFailed",
     "the requested allocation could not be satisfied", false},

    {ErrorCode::IoFailure, ErrorCategory::Environment, "IoFailure",
     "a file operation failed", true},
    {ErrorCode::PathInvalid, ErrorCategory::Environment, "PathInvalid",
     "the path is not usable", false},
    {ErrorCode::PathTooLong, ErrorCategory::Environment, "PathTooLong",
     "the path is longer than the bound allows", false},
    {ErrorCode::PathTraversalRejected, ErrorCategory::Environment,
     "PathTraversalRejected",
     "the path contains a traversal component", false},
    {ErrorCode::ReparsePointRejected, ErrorCategory::Environment,
     "ReparsePointRejected",
     "the path traverses a symbolic link, junction or other reparse point", false},
    {ErrorCode::PermissionDenied, ErrorCategory::Environment, "PermissionDenied",
     "the operating system refused access", false},
    {ErrorCode::FlushFailed, ErrorCategory::Environment, "FlushFailed",
     "the buffered data could not be flushed to durable storage", false},
    {ErrorCode::LockFailure, ErrorCategory::Environment, "LockFailure",
     "the operating-system lock operation failed", true},
    {ErrorCode::ProcessFailure, ErrorCategory::Environment, "ProcessFailure",
     "a child process could not be started, or ended abnormally", true},

    {ErrorCode::ShuttingDown, ErrorCategory::Lifecycle, "ShuttingDown",
     "the object is shutting down and accepts no new work", true},
    {ErrorCode::Cancelled, ErrorCategory::Lifecycle, "Cancelled",
     "the operation was cancelled before it completed", true},

    {ErrorCode::InternalError, ErrorCategory::Internal, "InternalError",
     "an invariant this library relies on was violated", false},
};

[[nodiscard]] const ErrorInfo* find_info(ErrorCode code) noexcept {
  for (const ErrorInfo& info : kErrorTable) {
    if (info.code == code) {
      return &info;
    }
  }
  return nullptr;
}

}  // namespace

std::string_view error_code_name(ErrorCode code) noexcept {
  const ErrorInfo* info = find_info(code);
  return info != nullptr ? info->name : std::string_view("UnknownErrorCode");
}

std::string_view error_category_name(ErrorCategory category) noexcept {
  switch (category) {
    case ErrorCategory::Ok:
      return "Ok";
    case ErrorCategory::Usage:
      return "Usage";
    case ErrorCategory::Input:
      return "Input";
    case ErrorCategory::Structure:
      return "Structure";
    case ErrorCategory::Reference:
      return "Reference";
    case ErrorCategory::Quantity:
      return "Quantity";
    case ErrorCategory::Policy:
      return "Policy";
    case ErrorCategory::Persistence:
      return "Persistence";
    case ErrorCategory::Resource:
      return "Resource";
    case ErrorCategory::Environment:
      return "Environment";
    case ErrorCategory::Lifecycle:
      return "Lifecycle";
    case ErrorCategory::Internal:
      return "Internal";
  }
  return "Unknown";
}

ErrorCategory category_of(ErrorCode code) noexcept {
  const ErrorInfo* info = find_info(code);
  return info != nullptr ? info->category : ErrorCategory::Internal;
}

std::string_view default_message(ErrorCode code) noexcept {
  const ErrorInfo* info = find_info(code);
  return info != nullptr ? info->message : std::string_view("unknown error code");
}

bool is_retryable(ErrorCode code) noexcept {
  const ErrorInfo* info = find_info(code);
  return info != nullptr && info->retryable;
}

Error Error::of(ErrorCode code) {
  Error error;
  error.code_ = code;
  error.category_ = category_of(code);
  error.message_ = std::string(default_message(code));
  return error;
}

Error Error::of(ErrorCode code, std::string message) {
  Error error;
  error.code_ = code;
  error.category_ = category_of(code);
  error.message_ = std::move(message);
  return error;
}

Error& Error::with_subject(std::string subject) {
  subject_ = std::move(subject);
  return *this;
}

Error& Error::with_detail(std::string detail) {
  detail_ = std::move(detail);
  return *this;
}

Error& Error::with_message(std::string message) {
  message_ = std::move(message);
  return *this;
}

std::string Error::to_string() const {
  std::string text(error_code_name(code_));
  text += ": ";
  text += message_;
  if (!subject_.empty()) {
    text += " (subject: ";
    text += subject_;
    text += ")";
  }
  if (!detail_.empty()) {
    text += " (detail: ";
    text += detail_;
    text += ")";
  }
  return text;
}

}  // namespace cooling_capacity_accounting
