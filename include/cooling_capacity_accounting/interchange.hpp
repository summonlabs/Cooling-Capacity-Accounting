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

#ifndef COOLING_CAPACITY_ACCOUNTING_INTERCHANGE_HPP
#define COOLING_CAPACITY_ACCOUNTING_INTERCHANGE_HPP

#include <string>
#include <string_view>

#include "cooling_capacity_accounting/errors.hpp"
#include "cooling_capacity_accounting/ledger.hpp"
#include "cooling_capacity_accounting/limits.hpp"

namespace cooling_capacity_accounting {

/// The cooling-capacity accounting interchange format is a strict, line
/// oriented, ASCII text form of one AccountingInput. It exists so that evidence
/// can be handed to this library by a human or by another system without a
/// binary toolchain, and so that a malformed hand-off is rejected with a precise
/// error instead of being guessed at.
///
/// The grammar, in order:
///
///   CCA-INTERCHANGE <version>                      exactly once, first line
///   GENERATIONS epoch=.. topology=.. policy=.. evidence=.. revision=..
///   POLICY id=.. generation=.. epoch=.. effective_from=.. ...
///   CLASS id=.. kind=.. medium=.. label=.. contributes=..
///   EVIDENCE id=.. kind=.. source_kind=.. source=.. observed_at=..
///                  recorded_at=.. generation=.. epoch=.. topology=..
///                  policy=.. medium=.. [value_mw=..] [subject_scope=..]
///                  [subject_equipment=..] [reference=..] [label=..]
///   DOMAIN id=.. scope=.. [evidence=..] [label=..] [epoch=..] [topology=..]
///   SCOPE id=.. kind=.. [parent=..] medium=.. [label=..] [declared_mw=..]
///   GROUP id=.. scope=.. classification=.. medium=.. [required=..]
///                 [redundancy=..] [protected_mw=..] [protected_evidence=..]
///                 [domains=a,b] [epoch=..] [topology=..] [policy=..]
///                 [evidence=..]
///   CONTRIBUTION id=.. scope=.. equipment=.. class=.. medium=..
///                classification=.. service=.. [installed_mw=.. |
///                installed=unknown reason=.. | installed=unsupported reason=..]
///                [priority=..] [group=..] [domain=..] [aliases=a,b]
///                [epoch=..] [topology=..] [policy=..] [evidence=..]
///                [evidence_refs=a,b] [observed_at=..] [sequence=..]
///                [label=..]
///   DERATE contribution=.. id=.. kind=factor|absolute ppm=.. | mw=..
///                evidence=.. [label=..]
///   SHARE contribution=.. target=.. ppm=..
///   MANIFEST id=.. scope=.. medium=.. declared_mw=.. evidence=..
///                [class=..] [observed_at=..] [epoch=..] [topology=..]
///                [policy=..] [evidence_generation=..]
///   END
///
/// Every keyword must be one this format defines and every required field must
/// be present exactly once. Records appear in non-decreasing rank order, where
/// the rank order is the record order above and DERATE and SHARE share the rank
/// of CONTRIBUTION, so a contribution's derates and shares may follow it
/// immediately or appear after every contribution. Nothing may follow END, and
/// a DERATE or SHARE may only name a contribution that has already been read.
/// Text is compared case sensitively and there are no blank lines.
[[nodiscard]] Result<AccountingInput> parse_interchange(std::string_view text,
                                                        const Limits& limits = Limits{});

/// Renders an input in the interchange format in canonical record order, so a
/// round trip is byte-stable.
[[nodiscard]] Result<std::string> write_interchange(const AccountingInput& input,
                                                    const Limits& limits = Limits{});

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_INTERCHANGE_HPP
