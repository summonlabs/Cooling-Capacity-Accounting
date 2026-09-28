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

// Proof obligations for the machine-readable failure contract. Every
// enumerator of ErrorCode is walked here: its stable name, its category and its
// retryability are pinned, and the exhaustive switch below fails to compile
// when a new enumerator is added without being added to this file.
//
// MSVC does not report C4062 ("enumerator in a switch of enum is not handled")
// at /W4 unless it is explicitly re-enabled, so the switch is wrapped in a
// warning push/pop. Under /WX a missing case is therefore a build failure, not
// a silently skipped obligation.

#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

namespace {

using cooling_capacity_accounting::category_of;
using cooling_capacity_accounting::default_message;
using cooling_capacity_accounting::Error;
using cooling_capacity_accounting::ErrorCategory;
using cooling_capacity_accounting::error_category_name;
using cooling_capacity_accounting::ErrorCode;
using cooling_capacity_accounting::error_code_name;
using cooling_capacity_accounting::is_retryable;
using cooling_capacity_accounting::Ok;
using cooling_capacity_accounting::Result;

/// The complete contract of one error code: its stable name, its category and
/// whether an identical retry can succeed.
struct CodeContract {
  ErrorCode code;
  std::string_view name;
  ErrorCategory category;
  bool retryable;
};

/// Every enumerator ErrorCode declares. A code that is missing here is refused
/// by the exhaustive switch below at compile time.
constexpr CodeContract kCodeContracts[] = {
      {ErrorCode::Ok, "Ok", ErrorCategory::Ok, false},
      {ErrorCode::InvalidArgument, "InvalidArgument", ErrorCategory::Usage, false},
      {ErrorCode::UnexpectedState, "UnexpectedState", ErrorCategory::Usage, false},
      {ErrorCode::NotSupported, "NotSupported", ErrorCategory::Usage, false},
      {ErrorCode::InvalidIdentifier, "InvalidIdentifier", ErrorCategory::Input, false},
      {ErrorCode::InvalidText, "InvalidText", ErrorCategory::Input, false},
      {ErrorCode::InvalidUtf8, "InvalidUtf8", ErrorCategory::Input, false},
      {ErrorCode::InvalidNumber, "InvalidNumber", ErrorCategory::Input, false},
      {ErrorCode::UnknownKeyword, "UnknownKeyword", ErrorCategory::Input, false},
      {ErrorCode::MissingRequiredField, "MissingRequiredField", ErrorCategory::Input, false},
      {ErrorCode::DuplicateField, "DuplicateField", ErrorCategory::Input, false},
      {ErrorCode::TooManyItems, "TooManyItems", ErrorCategory::Input, false},
      {ErrorCode::ItemTooLarge, "ItemTooLarge", ErrorCategory::Input, false},
      {ErrorCode::TruncatedInput, "TruncatedInput", ErrorCategory::Input, false},
      {ErrorCode::TrailingBytes, "TrailingBytes", ErrorCategory::Input, false},
      {ErrorCode::MalformedRecord, "MalformedRecord", ErrorCategory::Input, false},
      {ErrorCode::UnknownRecordKind, "UnknownRecordKind", ErrorCategory::Input, false},
      {ErrorCode::ImpossibleEnumValue, "ImpossibleEnumValue", ErrorCategory::Input, false},
      {ErrorCode::ReservedFieldNotZero, "ReservedFieldNotZero", ErrorCategory::Input, false},
      {ErrorCode::InvalidDigestText, "InvalidDigestText", ErrorCategory::Input, false},
      {ErrorCode::InvalidTimestampText, "InvalidTimestampText", ErrorCategory::Input, false},
      {ErrorCode::RecordOrderViolation, "RecordOrderViolation", ErrorCategory::Input, false},
      {ErrorCode::FutureTimestamp, "FutureTimestamp", ErrorCategory::Input, false},
      {ErrorCode::DuplicateIdentity, "DuplicateIdentity", ErrorCategory::Structure, false},
      {ErrorCode::DuplicateScope, "DuplicateScope", ErrorCategory::Structure, false},
      {ErrorCode::DuplicateEvidence, "DuplicateEvidence", ErrorCategory::Structure, false},
      {ErrorCode::DuplicateContribution, "DuplicateContribution", ErrorCategory::Structure, false},
      {ErrorCode::DuplicateIndependenceDomain, "DuplicateIndependenceDomain", ErrorCategory::Structure, false},
      {ErrorCode::UnknownScope, "UnknownScope", ErrorCategory::Structure, false},
      {ErrorCode::UnknownReference, "UnknownReference", ErrorCategory::Structure, false},
      {ErrorCode::CyclicScopeNesting, "CyclicScopeNesting", ErrorCategory::Structure, false},
      {ErrorCode::ScopeNestingTooDeep, "ScopeNestingTooDeep", ErrorCategory::Structure, false},
      {ErrorCode::OrphanScope, "OrphanScope", ErrorCategory::Structure, false},
      {ErrorCode::ScopeKindMismatch, "ScopeKindMismatch", ErrorCategory::Structure, false},
      {ErrorCode::MissingScopeKind, "MissingScopeKind", ErrorCategory::Structure, false},
      {ErrorCode::AmbiguousHomeScope, "AmbiguousHomeScope", ErrorCategory::Structure, false},
      {ErrorCode::MutuallyExclusiveGroupConflict, "MutuallyExclusiveGroupConflict", ErrorCategory::Structure, false},
      {ErrorCode::ContributionGroupMissing, "ContributionGroupMissing", ErrorCategory::Structure, false},
      {ErrorCode::GroupRequiresExclusiveSharing, "GroupRequiresExclusiveSharing", ErrorCategory::Structure, false},
      {ErrorCode::ShareTargetsHomeScope, "ShareTargetsHomeScope", ErrorCategory::Structure, false},
      {ErrorCode::DuplicateShareTarget, "DuplicateShareTarget", ErrorCategory::Structure, false},
      {ErrorCode::GroupScopeMismatch, "GroupScopeMismatch", ErrorCategory::Structure, false},
      {ErrorCode::ContributionOutsideClassBand, "ContributionOutsideClassBand", ErrorCategory::Structure, false},
      {ErrorCode::StaleGeneration, "StaleGeneration", ErrorCategory::Reference, false},
      {ErrorCode::FutureGeneration, "FutureGeneration", ErrorCategory::Reference, false},
      {ErrorCode::SupersededGeneration, "SupersededGeneration", ErrorCategory::Reference, false},
      {ErrorCode::StaleEpoch, "StaleEpoch", ErrorCategory::Reference, false},
      {ErrorCode::CrossEpochAuthority, "CrossEpochAuthority", ErrorCategory::Reference, false},
      {ErrorCode::GenerationMismatch, "GenerationMismatch", ErrorCategory::Reference, false},
      {ErrorCode::StaleEvidence, "StaleEvidence", ErrorCategory::Reference, false},
      {ErrorCode::StalePolicy, "StalePolicy", ErrorCategory::Reference, false},
      {ErrorCode::TopologyGenerationMismatch, "TopologyGenerationMismatch", ErrorCategory::Reference, false},
      {ErrorCode::PolicyGenerationMismatch, "PolicyGenerationMismatch", ErrorCategory::Reference, false},
      {ErrorCode::EvidenceGenerationMismatch, "EvidenceGenerationMismatch", ErrorCategory::Reference, false},
      {ErrorCode::StateRevisionMismatch, "StateRevisionMismatch", ErrorCategory::Reference, true},
      {ErrorCode::PublishPreconditionFailed, "PublishPreconditionFailed", ErrorCategory::Reference, true},
      {ErrorCode::SupersededWriter, "SupersededWriter", ErrorCategory::Reference, false},
      {ErrorCode::RecoveredStateRequiresRevalidation, "RecoveredStateRequiresRevalidation", ErrorCategory::Reference, false},
      {ErrorCode::NumericOverflow, "NumericOverflow", ErrorCategory::Quantity, false},
      {ErrorCode::NumericUnderflow, "NumericUnderflow", ErrorCategory::Quantity, false},
      {ErrorCode::DivisionByZero, "DivisionByZero", ErrorCategory::Quantity, false},
      {ErrorCode::OutOfRange, "OutOfRange", ErrorCategory::Quantity, false},
      {ErrorCode::NegativeQuantity, "NegativeQuantity", ErrorCategory::Quantity, false},
      {ErrorCode::ClosureViolation, "ClosureViolation", ErrorCategory::Quantity, false},
      {ErrorCode::DerateOutOfRange, "DerateOutOfRange", ErrorCategory::Quantity, false},
      {ErrorCode::WrongUnit, "WrongUnit", ErrorCategory::Quantity, false},
      {ErrorCode::UnknownMeasurement, "UnknownMeasurement", ErrorCategory::Policy, false},
      {ErrorCode::UnsupportedCapability, "UnsupportedCapability", ErrorCategory::Policy, false},
      {ErrorCode::MediumMismatch, "MediumMismatch", ErrorCategory::Policy, false},
      {ErrorCode::ClassMismatch, "ClassMismatch", ErrorCategory::Policy, false},
      {ErrorCode::IndependenceNotDeclared, "IndependenceNotDeclared", ErrorCategory::Policy, false},
      {ErrorCode::MissingIndependenceEvidence, "MissingIndependenceEvidence", ErrorCategory::Policy, false},
      {ErrorCode::ReserveShortfall, "ReserveShortfall", ErrorCategory::Policy, false},
      {ErrorCode::OverApportioned, "OverApportioned", ErrorCategory::Policy, false},
      {ErrorCode::ApportionmentIncomplete, "ApportionmentIncomplete", ErrorCategory::Policy, false},
      {ErrorCode::RequiredEvidenceMissing, "RequiredEvidenceMissing", ErrorCategory::Policy, false},
      {ErrorCode::PolicyViolation, "PolicyViolation", ErrorCategory::Policy, false},
      {ErrorCode::OutOfServiceWithoutEvidence, "OutOfServiceWithoutEvidence", ErrorCategory::Policy, false},
      {ErrorCode::ClassificationRequired, "ClassificationRequired", ErrorCategory::Policy, false},
      {ErrorCode::ConflictingContribution, "ConflictingContribution", ErrorCategory::Policy, false},
      {ErrorCode::UnresolvedResidual, "UnresolvedResidual", ErrorCategory::Policy, false},
      {ErrorCode::ReserveUnresolved, "ReserveUnresolved", ErrorCategory::Policy, false},
      {ErrorCode::NameplateMismatch, "NameplateMismatch", ErrorCategory::Policy, false},
      {ErrorCode::CoverageBelowMinimum, "CoverageBelowMinimum", ErrorCategory::Policy, false},
      {ErrorCode::NonContributingClass, "NonContributingClass", ErrorCategory::Policy, false},
      {ErrorCode::AliasOfUnknownContribution, "AliasOfUnknownContribution", ErrorCategory::Policy, false},
      {ErrorCode::StoreNotFound, "StoreNotFound", ErrorCategory::Persistence, false},
      {ErrorCode::StoreInUse, "StoreInUse", ErrorCategory::Persistence, true},
      {ErrorCode::WriterLockUnavailable, "WriterLockUnavailable", ErrorCategory::Persistence, true},
      {ErrorCode::IntegrityFailure, "IntegrityFailure", ErrorCategory::Persistence, false},
      {ErrorCode::DigestMismatch, "DigestMismatch", ErrorCategory::Persistence, false},
      {ErrorCode::UnsupportedFormatVersion, "UnsupportedFormatVersion", ErrorCategory::Persistence, false},
      {ErrorCode::NoPublishedGeneration, "NoPublishedGeneration", ErrorCategory::Persistence, false},
      {ErrorCode::PublishSequenceRegressed, "PublishSequenceRegressed", ErrorCategory::Persistence, false},
      {ErrorCode::IdempotencyConflict, "IdempotencyConflict", ErrorCategory::Persistence, false},
      {ErrorCode::GenerationNotRetained, "GenerationNotRetained", ErrorCategory::Persistence, false},
      {ErrorCode::PartialPublication, "PartialPublication", ErrorCategory::Persistence, false},
      {ErrorCode::StoreCorrupt, "StoreCorrupt", ErrorCategory::Persistence, false},
      {ErrorCode::AttemptMismatch, "AttemptMismatch", ErrorCategory::Persistence, false},
      {ErrorCode::LimitExceeded, "LimitExceeded", ErrorCategory::Resource, false},
      {ErrorCode::AllocationFailed, "AllocationFailed", ErrorCategory::Resource, false},
      {ErrorCode::IoFailure, "IoFailure", ErrorCategory::Environment, true},
      {ErrorCode::PathInvalid, "PathInvalid", ErrorCategory::Environment, false},
      {ErrorCode::PathTooLong, "PathTooLong", ErrorCategory::Environment, false},
      {ErrorCode::PathTraversalRejected, "PathTraversalRejected", ErrorCategory::Environment, false},
      {ErrorCode::ReparsePointRejected, "ReparsePointRejected", ErrorCategory::Environment, false},
      {ErrorCode::PermissionDenied, "PermissionDenied", ErrorCategory::Environment, false},
      {ErrorCode::FlushFailed, "FlushFailed", ErrorCategory::Environment, false},
      {ErrorCode::LockFailure, "LockFailure", ErrorCategory::Environment, true},
      {ErrorCode::ProcessFailure, "ProcessFailure", ErrorCategory::Environment, true},
      {ErrorCode::ShuttingDown, "ShuttingDown", ErrorCategory::Lifecycle, true},
      {ErrorCode::Cancelled, "Cancelled", ErrorCategory::Lifecycle, true},
      {ErrorCode::InternalError, "InternalError", ErrorCategory::Internal, false},
};

/// Number of enumerators declared by ErrorCode. Adding one is a deliberate act:
/// it forces an update here and in the exhaustive switch.
constexpr std::size_t kDeclaredErrorCodeCount = 113U;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(default : 4062)
#endif

/// Exhaustive over ErrorCode: no default label, so a new enumerator makes the
/// switch incomplete and /WX turns C4062 into a build failure.
[[nodiscard]] constexpr bool covered_by_exhaustive_switch(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Ok:
      return true;
    case ErrorCode::InvalidArgument:
      return true;
    case ErrorCode::UnexpectedState:
      return true;
    case ErrorCode::NotSupported:
      return true;
    case ErrorCode::InvalidIdentifier:
      return true;
    case ErrorCode::InvalidText:
      return true;
    case ErrorCode::InvalidUtf8:
      return true;
    case ErrorCode::InvalidNumber:
      return true;
    case ErrorCode::UnknownKeyword:
      return true;
    case ErrorCode::MissingRequiredField:
      return true;
    case ErrorCode::DuplicateField:
      return true;
    case ErrorCode::TooManyItems:
      return true;
    case ErrorCode::ItemTooLarge:
      return true;
    case ErrorCode::TruncatedInput:
      return true;
    case ErrorCode::TrailingBytes:
      return true;
    case ErrorCode::MalformedRecord:
      return true;
    case ErrorCode::UnknownRecordKind:
      return true;
    case ErrorCode::ImpossibleEnumValue:
      return true;
    case ErrorCode::ReservedFieldNotZero:
      return true;
    case ErrorCode::InvalidDigestText:
      return true;
    case ErrorCode::InvalidTimestampText:
      return true;
    case ErrorCode::RecordOrderViolation:
      return true;
    case ErrorCode::FutureTimestamp:
      return true;
    case ErrorCode::DuplicateIdentity:
      return true;
    case ErrorCode::DuplicateScope:
      return true;
    case ErrorCode::DuplicateEvidence:
      return true;
    case ErrorCode::DuplicateContribution:
      return true;
    case ErrorCode::DuplicateIndependenceDomain:
      return true;
    case ErrorCode::UnknownScope:
      return true;
    case ErrorCode::UnknownReference:
      return true;
    case ErrorCode::CyclicScopeNesting:
      return true;
    case ErrorCode::ScopeNestingTooDeep:
      return true;
    case ErrorCode::OrphanScope:
      return true;
    case ErrorCode::ScopeKindMismatch:
      return true;
    case ErrorCode::MissingScopeKind:
      return true;
    case ErrorCode::AmbiguousHomeScope:
      return true;
    case ErrorCode::MutuallyExclusiveGroupConflict:
      return true;
    case ErrorCode::ContributionGroupMissing:
      return true;
    case ErrorCode::GroupRequiresExclusiveSharing:
      return true;
    case ErrorCode::ShareTargetsHomeScope:
      return true;
    case ErrorCode::DuplicateShareTarget:
      return true;
    case ErrorCode::GroupScopeMismatch:
      return true;
    case ErrorCode::ContributionOutsideClassBand:
      return true;
    case ErrorCode::StaleGeneration:
      return true;
    case ErrorCode::FutureGeneration:
      return true;
    case ErrorCode::SupersededGeneration:
      return true;
    case ErrorCode::StaleEpoch:
      return true;
    case ErrorCode::CrossEpochAuthority:
      return true;
    case ErrorCode::GenerationMismatch:
      return true;
    case ErrorCode::StaleEvidence:
      return true;
    case ErrorCode::StalePolicy:
      return true;
    case ErrorCode::TopologyGenerationMismatch:
      return true;
    case ErrorCode::PolicyGenerationMismatch:
      return true;
    case ErrorCode::EvidenceGenerationMismatch:
      return true;
    case ErrorCode::StateRevisionMismatch:
      return true;
    case ErrorCode::PublishPreconditionFailed:
      return true;
    case ErrorCode::SupersededWriter:
      return true;
    case ErrorCode::RecoveredStateRequiresRevalidation:
      return true;
    case ErrorCode::NumericOverflow:
      return true;
    case ErrorCode::NumericUnderflow:
      return true;
    case ErrorCode::DivisionByZero:
      return true;
    case ErrorCode::OutOfRange:
      return true;
    case ErrorCode::NegativeQuantity:
      return true;
    case ErrorCode::ClosureViolation:
      return true;
    case ErrorCode::DerateOutOfRange:
      return true;
    case ErrorCode::WrongUnit:
      return true;
    case ErrorCode::UnknownMeasurement:
      return true;
    case ErrorCode::UnsupportedCapability:
      return true;
    case ErrorCode::MediumMismatch:
      return true;
    case ErrorCode::ClassMismatch:
      return true;
    case ErrorCode::IndependenceNotDeclared:
      return true;
    case ErrorCode::MissingIndependenceEvidence:
      return true;
    case ErrorCode::ReserveShortfall:
      return true;
    case ErrorCode::OverApportioned:
      return true;
    case ErrorCode::ApportionmentIncomplete:
      return true;
    case ErrorCode::RequiredEvidenceMissing:
      return true;
    case ErrorCode::PolicyViolation:
      return true;
    case ErrorCode::OutOfServiceWithoutEvidence:
      return true;
    case ErrorCode::ClassificationRequired:
      return true;
    case ErrorCode::ConflictingContribution:
      return true;
    case ErrorCode::UnresolvedResidual:
      return true;
    case ErrorCode::ReserveUnresolved:
      return true;
    case ErrorCode::NameplateMismatch:
      return true;
    case ErrorCode::CoverageBelowMinimum:
      return true;
    case ErrorCode::NonContributingClass:
      return true;
    case ErrorCode::AliasOfUnknownContribution:
      return true;
    case ErrorCode::StoreNotFound:
      return true;
    case ErrorCode::StoreInUse:
      return true;
    case ErrorCode::WriterLockUnavailable:
      return true;
    case ErrorCode::IntegrityFailure:
      return true;
    case ErrorCode::DigestMismatch:
      return true;
    case ErrorCode::UnsupportedFormatVersion:
      return true;
    case ErrorCode::NoPublishedGeneration:
      return true;
    case ErrorCode::PublishSequenceRegressed:
      return true;
    case ErrorCode::IdempotencyConflict:
      return true;
    case ErrorCode::GenerationNotRetained:
      return true;
    case ErrorCode::PartialPublication:
      return true;
    case ErrorCode::StoreCorrupt:
      return true;
    case ErrorCode::AttemptMismatch:
      return true;
    case ErrorCode::LimitExceeded:
      return true;
    case ErrorCode::AllocationFailed:
      return true;
    case ErrorCode::IoFailure:
      return true;
    case ErrorCode::PathInvalid:
      return true;
    case ErrorCode::PathTooLong:
      return true;
    case ErrorCode::PathTraversalRejected:
      return true;
    case ErrorCode::ReparsePointRejected:
      return true;
    case ErrorCode::PermissionDenied:
      return true;
    case ErrorCode::FlushFailed:
      return true;
    case ErrorCode::LockFailure:
      return true;
    case ErrorCode::ProcessFailure:
      return true;
    case ErrorCode::ShuttingDown:
      return true;
    case ErrorCode::Cancelled:
      return true;
    case ErrorCode::InternalError:
      return true;
  }
  return false;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

/// One propagation helper: the failure has to survive the CCA_TRY round trip.
[[nodiscard]] Result<int> propagate_failure(ErrorCode code) {
  CCA_TRY(Result<void>(Error::of(code).with_subject("inner")));
  return 42;
}

[[nodiscard]] Result<int> outer() {
  CCA_TRY_ASSIGN(inner, propagate_failure(ErrorCode::TruncatedInput));
  return inner + 1;
}

}  // namespace

CCA_TEST(every_error_code_has_a_stable_contract) {
  CCA_CHECK_EQ(std::size(kCodeContracts), kDeclaredErrorCodeCount);
  for (const CodeContract& contract : kCodeContracts) {
    // The switch is exhaustive at compile time; the call makes it load bearing.
    CCA_CHECK(covered_by_exhaustive_switch(contract.code));
    CCA_CHECK(error_code_name(contract.code) != std::string_view("UnknownErrorCode"));
    CCA_CHECK_EQ(error_code_name(contract.code), contract.name);
    CCA_CHECK_EQ(category_of(contract.code), contract.category);
    CCA_CHECK(error_category_name(contract.category) != std::string_view("Unknown"));
    CCA_CHECK(!default_message(contract.code).empty());
    CCA_CHECK_EQ(is_retryable(contract.code), contract.retryable);
  }
}

CCA_TEST(error_code_rows_are_unique_and_only_internal_error_is_internal) {
  for (std::size_t left = 0; left < std::size(kCodeContracts); ++left) {
    for (std::size_t right = left + 1U; right < std::size(kCodeContracts); ++right) {
      if (kCodeContracts[left].code == kCodeContracts[right].code) {
        CCA_FAIL("the declared error table repeats a code");
        return;
      }
    }
    if (kCodeContracts[left].code != ErrorCode::InternalError) {
      CCA_CHECK(category_of(kCodeContracts[left].code) != ErrorCategory::Internal);
    }
  }
  CCA_CHECK_EQ(category_of(ErrorCode::InternalError), ErrorCategory::Internal);
  CCA_CHECK(!is_retryable(ErrorCode::Ok));
  CCA_CHECK_EQ(default_message(ErrorCode::Ok), std::string_view("success"));
  CCA_CHECK_EQ(error_code_name(ErrorCode::Ok), std::string_view("Ok"));
}

CCA_TEST(undeclared_error_codes_fall_back_safely) {
  const ErrorCode unknown = static_cast<ErrorCode>(123456);
  CCA_CHECK_EQ(error_code_name(unknown), std::string_view("UnknownErrorCode"));
  CCA_CHECK_EQ(category_of(unknown), ErrorCategory::Internal);
  CCA_CHECK(!default_message(unknown).empty());
  CCA_CHECK(!is_retryable(unknown));
  CCA_CHECK_EQ(error_category_name(static_cast<ErrorCategory>(99)),
               std::string_view("Unknown"));
}

CCA_TEST(error_category_names_are_stable) {
  struct CategoryCase {
    ErrorCategory category;
    std::string_view name;
  };
  const CategoryCase categories[] = {
      {ErrorCategory::Ok, "Ok"},
      {ErrorCategory::Usage, "Usage"},
      {ErrorCategory::Input, "Input"},
      {ErrorCategory::Structure, "Structure"},
      {ErrorCategory::Reference, "Reference"},
      {ErrorCategory::Quantity, "Quantity"},
      {ErrorCategory::Policy, "Policy"},
      {ErrorCategory::Persistence, "Persistence"},
      {ErrorCategory::Resource, "Resource"},
      {ErrorCategory::Environment, "Environment"},
      {ErrorCategory::Lifecycle, "Lifecycle"},
      {ErrorCategory::Internal, "Internal"},
  };
  for (const CategoryCase& item : categories) {
    CCA_CHECK_EQ(error_category_name(item.category), item.name);
    CCA_CHECK(!error_category_name(item.category).empty());
  }
  for (std::size_t left = 0; left < std::size(categories); ++left) {
    for (std::size_t right = left + 1U; right < std::size(categories); ++right) {
      CCA_CHECK(categories[left].category != categories[right].category);
      CCA_CHECK(error_category_name(categories[left].category) !=
                error_category_name(categories[right].category));
    }
  }
}

CCA_TEST(error_of_rendering_subject_and_detail) {
  const Error plain = Error::of(ErrorCode::InvalidIdentifier);
  CCA_CHECK(!plain.ok());
  CCA_CHECK_EQ(plain.code(), ErrorCode::InvalidIdentifier);
  CCA_CHECK_EQ(plain.category(), ErrorCategory::Input);
  CCA_CHECK_EQ(plain.message(),
               std::string(default_message(ErrorCode::InvalidIdentifier)));
  CCA_CHECK(plain.subject().empty());
  CCA_CHECK(plain.detail().empty());
  CCA_CHECK_EQ(plain.to_string(),
               std::string("InvalidIdentifier: ") +
                   std::string(default_message(ErrorCode::InvalidIdentifier)));

  const Error custom =
      Error::of(ErrorCode::InvalidIdentifier, "the identifier is not well formed");
  CCA_CHECK_EQ(custom.message(), std::string("the identifier is not well formed"));
  CCA_CHECK_EQ(custom.to_string(),
               std::string("InvalidIdentifier: the identifier is not well formed"));

  Error decorated = Error::of(ErrorCode::OutOfRange, "quantity out of range");
  decorated.with_subject("installed.mw").with_detail("observed 1000000000000001");
  CCA_CHECK_EQ(decorated.subject(), std::string("installed.mw"));
  CCA_CHECK_EQ(decorated.detail(), std::string("observed 1000000000000001"));
  CCA_CHECK_EQ(decorated.to_string(),
               std::string("OutOfRange: quantity out of range"
                           " (subject: installed.mw)"
                           " (detail: observed 1000000000000001)"));

  // Empty decorations are not rendered at all.
  Error bare = Error::of(ErrorCode::OutOfRange, "message");
  bare.with_subject(std::string()).with_detail(std::string());
  CCA_CHECK_EQ(bare.to_string(), std::string("OutOfRange: message"));

  // with_message replaces the human text and keeps the machine code.
  Error replaced = Error::of(ErrorCode::OutOfRange, "first");
  replaced.with_message("second");
  CCA_CHECK_EQ(replaced.code(), ErrorCode::OutOfRange);
  CCA_CHECK_EQ(replaced.category(), ErrorCategory::Quantity);
  CCA_CHECK_EQ(replaced.message(), std::string("second"));
  CCA_CHECK_EQ(replaced.to_string(), std::string("OutOfRange: second"));

  // Equality is the machine contract: the human message is not part of it.
  CCA_CHECK(Error::of(ErrorCode::OutOfRange, "a") ==
            Error::of(ErrorCode::OutOfRange, "b"));
  CCA_CHECK(Error::of(ErrorCode::OutOfRange).with_subject("s") ==
            Error::of(ErrorCode::OutOfRange).with_subject("s"));
  CCA_CHECK(Error::of(ErrorCode::OutOfRange).with_subject("s") !=
            Error::of(ErrorCode::OutOfRange).with_subject("t"));
  CCA_CHECK(Error::of(ErrorCode::OutOfRange).with_detail("d") !=
            Error::of(ErrorCode::OutOfRange));
  CCA_CHECK(Error::of(ErrorCode::OutOfRange) !=
            Error::of(ErrorCode::NumericOverflow));
  CCA_CHECK(Error::of(ErrorCode::OutOfRange) == Error::of(ErrorCode::OutOfRange));

  // A default-constructed Error is success, and it carries no text of its own:
  // only Error::of installs the default message for its code.
  const Error none;
  CCA_CHECK(none.ok());
  CCA_CHECK_EQ(none.code(), ErrorCode::Ok);
  CCA_CHECK_EQ(none.category(), ErrorCategory::Ok);
  CCA_CHECK(none.message().empty());
  CCA_CHECK_EQ(none.to_string(), std::string("Ok: "));
  const Error defaulted = Error::of(ErrorCode::Ok);
  CCA_CHECK(defaulted.ok());
  CCA_CHECK_EQ(defaulted.to_string(), std::string("Ok: success"));
  // Success is decided by the code alone, never by the human message.
  const Error success_with_text = Error::of(ErrorCode::Ok, "still success");
  CCA_CHECK(success_with_text.ok());
  CCA_CHECK_EQ(success_with_text.message(), std::string("still success"));
}

CCA_TEST(result_carries_the_value_or_the_error) {
  const Result<int> good(7);
  CCA_CHECK(good.ok());
  CCA_CHECK(static_cast<bool>(good));
  CCA_CHECK_EQ(good.code(), ErrorCode::Ok);
  CCA_CHECK_EQ(good.value(), 7);
  CCA_CHECK_EQ(good.value_or(9), 7);

  const Result<int> bad(Error::of(ErrorCode::OutOfRange).with_subject("installed"));
  CCA_CHECK(!bad.ok());
  CCA_CHECK(!static_cast<bool>(bad));
  CCA_CHECK_EQ(bad.code(), ErrorCode::OutOfRange);
  CCA_CHECK_EQ(bad.error().subject(), std::string("installed"));
  CCA_CHECK_EQ(bad.value_or(9), 9);
  CCA_CHECK(bad.error().to_string().find("OutOfRange") != std::string::npos);

  const Result<std::string> text(std::string("value"));
  CCA_CHECK(text.ok());
  CCA_CHECK_EQ(text.value(), std::string("value"));
  CCA_CHECK_EQ(text.value_or("fallback"), std::string("value"));
  const Result<std::string> empty_error(Error::of(ErrorCode::InvalidText));
  CCA_CHECK(!empty_error.ok());
  CCA_CHECK_EQ(empty_error.value_or("fallback"), std::string("fallback"));
  CCA_CHECK_EQ(empty_error.code(), ErrorCode::InvalidText);

  const Result<void> done(Ok{});
  CCA_CHECK(done.ok());
  CCA_CHECK_EQ(done.code(), ErrorCode::Ok);
  const Result<void> failed(
      Error::of(ErrorCode::ClosureViolation).with_detail("scope site.alpha"));
  CCA_CHECK(!failed.ok());
  CCA_CHECK_EQ(failed.code(), ErrorCode::ClosureViolation);
  CCA_CHECK_EQ(failed.error().detail(), std::string("scope site.alpha"));

  // take() moves the value out of a successful result.
  Result<std::string> movable(std::string("moved"));
  CCA_CHECK(movable.ok());
  CCA_CHECK_EQ(movable.take(), std::string("moved"));

  // The error survives the CCA_TRY and CCA_TRY_ASSIGN round trips.
  const Result<int> propagated = propagate_failure(ErrorCode::TruncatedInput);
  CCA_CHECK(!propagated.ok());
  CCA_CHECK_EQ(propagated.code(), ErrorCode::TruncatedInput);
  CCA_CHECK_EQ(propagated.error().subject(), std::string("inner"));
  const Result<int> from_outer = outer();
  CCA_CHECK(!from_outer.ok());
  CCA_CHECK_EQ(from_outer.code(), ErrorCode::TruncatedInput);
  CCA_CHECK_EQ(from_outer.error().subject(), std::string("inner"));
}

CCA_TEST(retryability_is_declared_per_code) {
  CCA_CHECK(!is_retryable(ErrorCode::Ok));

  const ErrorCode retryable[] = {
      ErrorCode::StateRevisionMismatch, ErrorCode::PublishPreconditionFailed,
      ErrorCode::StoreInUse,           ErrorCode::WriterLockUnavailable,
      ErrorCode::IoFailure,            ErrorCode::LockFailure,
      ErrorCode::ProcessFailure,       ErrorCode::ShuttingDown,
      ErrorCode::Cancelled,
  };
  for (const ErrorCode code : retryable) {
    CCA_CHECK(is_retryable(code));
  }

  const ErrorCode permanent[] = {
      ErrorCode::InvalidIdentifier, ErrorCode::InvalidNumber,
      ErrorCode::DuplicateIdentity, ErrorCode::StaleGeneration,
      ErrorCode::NumericOverflow,   ErrorCode::DivisionByZero,
      ErrorCode::StoreCorrupt,      ErrorCode::LimitExceeded,
      ErrorCode::PolicyViolation,   ErrorCode::InternalError,
  };
  for (const ErrorCode code : permanent) {
    CCA_CHECK(!is_retryable(code));
  }

  std::size_t retryable_count = 0;
  for (const CodeContract& contract : kCodeContracts) {
    if (contract.retryable) {
      retryable_count += 1U;
    }
  }
  CCA_CHECK_EQ(retryable_count, 9U);
  CCA_CHECK_EQ(std::size(retryable), retryable_count);
}
