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

#include <algorithm>
#include <iostream>
#include <string>

#include "example_support.hpp"

// 08 - interchange text and the canonical byte form.
//
// Every facility fact used here is SYNTHETIC.


int main() {
  std::cout << "08 - interchange and canonical form (SYNTHETIC data)\n";
  example::AccountingInput input = example::make_input();
  input.equipment_classes.push_back(example::make_class(
      "class.crah", example::Medium::Air,
      example::EquipmentClassKind::ComputerRoomAirHandler));
  input.scopes.push_back(
      example::make_scope("site.alpha", example::ScopeKind::Site, "", example::Medium::Air));
  input.scopes.push_back(example::make_scope("loop.a", example::ScopeKind::Loop,
                                             "site.alpha", example::Medium::Air));
  input.evidence.push_back(example::make_evidence("evidence.one", 250'000));
  input.contributions.push_back(
      example::make_unit("unit.one", "loop.a", "class.crah", 250'000, "evidence.one"));

  EXAMPLE_ASSIGN(text, example::write_interchange(input));
  std::cout << "  interchange text is " << text.size() << " bytes, "
            << std::count(text.begin(), text.end(), '\n') << " records\n";

  EXAMPLE_ASSIGN(reparsed, example::parse_interchange(text));
  EXAMPLE_ASSIGN(rerendered, example::write_interchange(reparsed));
  EXAMPLE_REQUIRE(text == rerendered,
                  "the interchange text round trips byte for byte");

  EXAMPLE_ASSIGN(canonical, example::encode_canonical(input));
  EXAMPLE_ASSIGN(decoded, example::decode_canonical(canonical));
  EXAMPLE_ASSIGN(reencoded, example::encode_canonical(decoded));
  EXAMPLE_REQUIRE(canonical == reencoded,
                  "the canonical form round trips byte for byte");

  // Shuffling every input vector must not change one byte of the canonical
  // form: canonical means order independent.
  example::AccountingInput shuffled = input;
  std::swap(shuffled.scopes[0], shuffled.scopes[1]);
  std::reverse(shuffled.contributions.begin(), shuffled.contributions.end());
  std::reverse(shuffled.evidence.begin(), shuffled.evidence.end());
  EXAMPLE_ASSIGN(shuffled_bytes, example::encode_canonical(shuffled));
  EXAMPLE_REQUIRE(canonical == shuffled_bytes,
                  "the canonical form does not depend on submission order");

  EXAMPLE_ASSIGN(digest, example::canonical_digest(input));
  std::cout << "  canonical digest " << digest.to_hex() << "\n";

  // A document that loses its final record is refused as truncated.
  const std::string without_end = text.substr(0, text.size() - 4U);
  const example::Result<example::AccountingInput> truncated =
      example::parse_interchange(without_end);
  EXAMPLE_REQUIRE(!truncated.ok(), "a document without END is refused");
  EXAMPLE_REQUIRE(truncated.error().code() == example::ErrorCode::TruncatedInput,
                  "a document that does not end with END is TruncatedInput");
  std::cout << "  missing END refused with "
            << example::error_code_name(truncated.error().code()) << "\n";

  // A document cut in the middle is refused too, with whatever precise code
  // describes the first thing that is wrong with it.
  const std::string cut = text.substr(0, text.size() / 2U);
  const example::Result<example::AccountingInput> rejected =
      example::parse_interchange(cut);
  EXAMPLE_REQUIRE(!rejected.ok(), "a document cut in the middle is refused");
  std::cout << "  truncated document refused with "
            << example::error_code_name(rejected.error().code()) << ": "
            << rejected.error().to_string() << "\n";

  const std::string unknown_keyword = text + "NONSENSE field=1\n";
  const example::Result<example::AccountingInput> trailing =
      example::parse_interchange(unknown_keyword);
  EXAMPLE_REQUIRE(!trailing.ok(), "text after END is refused");
  EXAMPLE_REQUIRE(trailing.error().code() == example::ErrorCode::TrailingBytes,
                  "text after END is TrailingBytes");
  std::cout << "  OK\n";
  return 0;
}
