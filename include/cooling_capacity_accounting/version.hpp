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

#ifndef COOLING_CAPACITY_ACCOUNTING_VERSION_HPP
#define COOLING_CAPACITY_ACCOUNTING_VERSION_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace cooling_capacity_accounting {

/// Major version of the Cooling Capacity Accounting public interface.
inline constexpr int kVersionMajor = 1;
/// Minor version of the Cooling Capacity Accounting public interface.
inline constexpr int kVersionMinor = 0;
/// Patch version of the Cooling Capacity Accounting public interface.
inline constexpr int kVersionPatch = 0;

/// Product name, as it appears in reports and package metadata.
inline constexpr std::string_view kProductName = "Cooling Capacity Accounting";
/// Repository slug of this runtime.
inline constexpr std::string_view kRepositorySlug = "Cooling-Capacity-Accounting";
/// Program position of this runtime.
inline constexpr std::string_view kProgramPosition =
    "Data Center Control Plane, Tranche 2 - facility capacity and placement";

/// Format version of the canonical record encoding produced by
/// cooling_capacity_accounting::encode_canonical.
inline constexpr std::uint32_t kCanonicalFormatVersion = 1;
/// Format version of the durable store written by AccountingStore.
inline constexpr std::uint32_t kStoreFormatVersion = 1;
/// Format version of the interchange text accepted by parse_interchange.
inline constexpr std::uint32_t kInterchangeFormatVersion = 1;

/// "major.minor.patch".
[[nodiscard]] std::string version_string();
/// Product name, version and program position in one stable line.
[[nodiscard]] std::string version_banner();

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_VERSION_HPP
