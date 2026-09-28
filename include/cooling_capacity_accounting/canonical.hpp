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

#ifndef COOLING_CAPACITY_ACCOUNTING_CANONICAL_HPP
#define COOLING_CAPACITY_ACCOUNTING_CANONICAL_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cooling_capacity_accounting/digest.hpp"
#include "cooling_capacity_accounting/errors.hpp"
#include "cooling_capacity_accounting/limits.hpp"
#include "cooling_capacity_accounting/ledger.hpp"

namespace cooling_capacity_accounting {

/// Encodes an accounting input into its canonical byte form.
///
/// Canonical means: a self-describing header, fixed-width big-endian integers,
/// length-prefixed variable-length fields, records in a documented total order,
/// and no dependence on the order the caller supplied its vectors in. Two
/// inputs that differ only in the order of their vectors encode to identical
/// bytes.
[[nodiscard]] Result<std::vector<std::uint8_t>> encode_canonical(
    const AccountingInput& input, const Limits& limits = Limits{});

/// Decodes canonical bytes. Every field is bounds-checked, every enumeration is
/// validated, duplicate identities are rejected and trailing bytes are an
/// error: a byte string that was not produced by encode_canonical is refused
/// rather than repaired.
[[nodiscard]] Result<AccountingInput> decode_canonical(const void* data,
                                                       std::size_t size,
                                                       const Limits& limits = Limits{});
[[nodiscard]] Result<AccountingInput> decode_canonical(
    const std::vector<std::uint8_t>& bytes, const Limits& limits = Limits{});

/// SHA-256 of the canonical encoding.
[[nodiscard]] Result<Digest> canonical_digest(const AccountingInput& input,
                                              const Limits& limits = Limits{});

/// The first bytes of the canonical watermark, for diagnostics.
inline constexpr std::string_view kCanonicalMagic = "CCAIN001";

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_CANONICAL_HPP
