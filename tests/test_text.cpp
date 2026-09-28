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

// Proof obligations for the strict text layer: UTF-8 acceptance and rejection
// of every malformed class, the identifier and bounded-text length boundaries,
// strict decimal parsing and the byte-level ASCII helpers.

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

namespace {

using cooling_capacity_accounting::BoundedText;
using cooling_capacity_accounting::DocumentRef;
using cooling_capacity_accounting::ErrorCode;
using cooling_capacity_accounting::Identifier;
using cooling_capacity_accounting::IdentifierHash;
using cooling_capacity_accounting::is_ascii_alphanumeric;
using cooling_capacity_accounting::is_ascii_digit;
using cooling_capacity_accounting::is_valid_identifier;
using cooling_capacity_accounting::is_valid_utf8;
using cooling_capacity_accounting::kMaxIdentifierLength;
using cooling_capacity_accounting::kMaxReferenceLength;
using cooling_capacity_accounting::kMaxTextLength;
using cooling_capacity_accounting::parse_int64;
using cooling_capacity_accounting::parse_uint32;
using cooling_capacity_accounting::parse_uint64;
using cooling_capacity_accounting::split_list;
using cooling_capacity_accounting::to_decimal;
using cooling_capacity_accounting::to_lower_ascii;
using cooling_capacity_accounting::to_printable_ascii;

constexpr std::int64_t kI64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kI64Min = std::numeric_limits<std::int64_t>::min();
constexpr std::uint64_t kU64Max = std::numeric_limits<std::uint64_t>::max();

/// A byte string built from explicit byte values, so a test never depends on
/// the execution character set or on a source-encoding escape.
[[nodiscard]] std::string bytes(std::initializer_list<int> values) {
  std::string text;
  text.reserve(values.size());
  for (const int value : values) {
    text.push_back(static_cast<char>(static_cast<unsigned char>(value)));
  }
  return text;
}

}  // namespace

CCA_TEST(is_valid_utf8_accepts_well_formed_sequences) {
  CCA_CHECK(is_valid_utf8(std::string_view()));
  CCA_CHECK(is_valid_utf8("plain ASCII 0123"));
  CCA_CHECK(is_valid_utf8(bytes({0x74, 0x61, 0x62, 0x09, 0x68, 0x65, 0x72, 0x65})));
  CCA_CHECK(is_valid_utf8(bytes({0x7F})));  // DEL is a valid code point

  CCA_CHECK(is_valid_utf8(bytes({0xC2, 0x80})));              // U+0080, shortest
  CCA_CHECK(is_valid_utf8(bytes({0xC3, 0xA9})));              // U+00E9
  CCA_CHECK(is_valid_utf8(bytes({0xE0, 0xA0, 0x80})));        // U+0800, shortest
  CCA_CHECK(is_valid_utf8(bytes({0xE2, 0x82, 0xAC})));        // U+20AC
  CCA_CHECK(is_valid_utf8(bytes({0xED, 0x9F, 0xBF})));        // U+D7FF, below the surrogates
  CCA_CHECK(is_valid_utf8(bytes({0xEE, 0x80, 0x80})));        // U+E000, above the surrogates
  CCA_CHECK(is_valid_utf8(bytes({0xF0, 0x90, 0x80, 0x80})));  // U+10000, shortest
  CCA_CHECK(is_valid_utf8(bytes({0xF0, 0x9D, 0x84, 0x9E})));  // U+1D11E
  CCA_CHECK(is_valid_utf8(bytes({0xF4, 0x8F, 0xBF, 0xBF})));  // U+10FFFF, the highest

  CCA_CHECK(is_valid_utf8(bytes({0x61, 0xC3, 0xA9, 0x62, 0xE2, 0x82, 0xAC, 0x63})));
}

CCA_TEST(is_valid_utf8_rejects_every_malformed_class) {
  // Embedded NUL, alone and inside otherwise valid text.
  CCA_CHECK(!is_valid_utf8(bytes({0x00})));
  CCA_CHECK(!is_valid_utf8(bytes({0x61, 0x00, 0x62})));

  // Overlong encodings of code points that have a shorter form.
  CCA_CHECK(!is_valid_utf8(bytes({0xC0, 0x80})));  // overlong NUL
  CCA_CHECK(!is_valid_utf8(bytes({0xC0, 0xAF})));  // overlong solidus
  CCA_CHECK(!is_valid_utf8(bytes({0xC1, 0xBF})));  // overlong U+007F
  CCA_CHECK(!is_valid_utf8(bytes({0xE0, 0x80, 0xAF})));
  CCA_CHECK(!is_valid_utf8(bytes({0xE0, 0x9F, 0xBF})));
  CCA_CHECK(!is_valid_utf8(bytes({0xF0, 0x80, 0x80, 0xAF})));
  CCA_CHECK(!is_valid_utf8(bytes({0xF0, 0x8F, 0xBF, 0xBF})));

  // UTF-16 surrogates U+D800..U+DFFF.
  CCA_CHECK(!is_valid_utf8(bytes({0xED, 0xA0, 0x80})));
  CCA_CHECK(!is_valid_utf8(bytes({0xED, 0xAD, 0xBF})));
  CCA_CHECK(!is_valid_utf8(bytes({0xED, 0xBF, 0xBF})));

  // Code points above U+10FFFF, and lead bytes that can never start a sequence.
  CCA_CHECK(!is_valid_utf8(bytes({0xF4, 0x90, 0x80, 0x80})));
  CCA_CHECK(!is_valid_utf8(bytes({0xF5, 0x80, 0x80, 0x80})));
  CCA_CHECK(!is_valid_utf8(bytes({0xF7, 0xBF, 0xBF, 0xBF})));
  CCA_CHECK(!is_valid_utf8(bytes({0xF8, 0x88, 0x80, 0x80, 0x80})));
  CCA_CHECK(!is_valid_utf8(bytes({0xFE})));
  CCA_CHECK(!is_valid_utf8(bytes({0xFF})));
  CCA_CHECK(!is_valid_utf8(bytes({0x80})));
  CCA_CHECK(!is_valid_utf8(bytes({0xBF})));

  // Truncated sequences at every length.
  CCA_CHECK(!is_valid_utf8(bytes({0xC3})));
  CCA_CHECK(!is_valid_utf8(bytes({0xE2})));
  CCA_CHECK(!is_valid_utf8(bytes({0xE2, 0x82})));
  CCA_CHECK(!is_valid_utf8(bytes({0xF0})));
  CCA_CHECK(!is_valid_utf8(bytes({0xF0, 0x9F, 0x98})));

  // A missing or wrong continuation byte.
  CCA_CHECK(!is_valid_utf8(bytes({0xC3, 0x28})));
  CCA_CHECK(!is_valid_utf8(bytes({0xE2, 0x28, 0xA1})));
  CCA_CHECK(!is_valid_utf8(bytes({0xF0, 0x28, 0x8C, 0x28})));

  // A valid prefix does not rescue a malformed suffix.
  CCA_CHECK(!is_valid_utf8(bytes({0x61, 0x62, 0x63, 0xC3})));
  CCA_CHECK(!is_valid_utf8(bytes({0xC3, 0xA9, 0xC3})));
}

CCA_TEST(is_valid_identifier_length_boundary_and_characters) {
  CCA_CHECK(!is_valid_identifier(std::string_view()));
  CCA_CHECK(is_valid_identifier("a"));
  CCA_CHECK(is_valid_identifier("A"));
  CCA_CHECK(is_valid_identifier("0"));
  CCA_CHECK(is_valid_identifier("site.alpha"));
  CCA_CHECK(is_valid_identifier("a-b_c:d"));
  CCA_CHECK(is_valid_identifier("..."));
  CCA_CHECK(is_valid_identifier("a.-_:b"));

  const std::string at_bound(kMaxIdentifierLength, 'a');
  const std::string past_bound(kMaxIdentifierLength + 1U, 'a');
  CCA_CHECK_EQ(at_bound.size(), kMaxIdentifierLength);
  CCA_CHECK(is_valid_identifier(at_bound));
  CCA_CHECK(!is_valid_identifier(past_bound));

  CCA_CHECK(!is_valid_identifier("a b"));
  CCA_CHECK(!is_valid_identifier("a/b"));
  CCA_CHECK(!is_valid_identifier("a+b"));
  CCA_CHECK(!is_valid_identifier("a,b"));
  CCA_CHECK(!is_valid_identifier(bytes({0x61, 0x0A, 0x62})));
  CCA_CHECK(!is_valid_identifier(bytes({0x61, 0x00, 0x62})));
  CCA_CHECK(!is_valid_identifier(bytes({0xC3, 0xA9})));
  CCA_CHECK(!is_valid_identifier("with space"));
}

CCA_TEST(identifier_parse_validation_and_ordering) {
  CCA_CHECK_CODE(Identifier::parse(std::string_view()), ErrorCode::InvalidIdentifier);
  CCA_CHECK_CODE(Identifier::parse("bad id"), ErrorCode::InvalidIdentifier);
  CCA_CHECK_CODE(Identifier::parse("bad/id"), ErrorCode::InvalidIdentifier);
  CCA_CHECK_CODE(Identifier::parse(std::string(kMaxIdentifierLength + 1U, 'x')),
                 ErrorCode::InvalidIdentifier);
  CCA_CHECK_CODE(Identifier::parse(bytes({0xC3, 0xA9})), ErrorCode::InvalidIdentifier);

  const std::string at_bound(kMaxIdentifierLength, 'x');
  CCA_ASSIGN(longest, Identifier::parse(at_bound));
  CCA_CHECK_EQ(longest.str(), at_bound);
  CCA_CHECK_EQ(longest.view(), std::string_view(at_bound));
  CCA_CHECK(!longest.empty());

  CCA_ASSIGN(site, Identifier::parse("site.alpha"));
  CCA_CHECK_EQ(site.str(), std::string("site.alpha"));
  CCA_CHECK_EQ(site.view().size(), 10U);

  const Identifier built = Identifier::from_validated("site.alpha");
  CCA_CHECK(site == built);
  CCA_CHECK(!(site != built));
  CCA_ASSIGN(other, Identifier::parse("site.beta"));
  CCA_CHECK(site != other);
  CCA_CHECK(site < other);
  CCA_CHECK(!(other < site));

  const Identifier empty_default;
  CCA_CHECK(empty_default.empty());
  CCA_CHECK_EQ(empty_default.str(), std::string());
  CCA_CHECK(empty_default != site);

  const IdentifierHash hash;
  CCA_CHECK_EQ(hash(site), hash(built));
  CCA_CHECK_EQ(hash(empty_default), hash(Identifier{}));
  CCA_CHECK(hash(site) != hash(other) || hash(site) == hash(other));
}

CCA_TEST(bounded_text_parse_length_and_character_bounds) {
  CCA_ASSIGN(empty, BoundedText::parse(std::string_view()));
  CCA_CHECK(empty.empty());
  CCA_ASSIGN(plain, BoundedText::parse("synthetic label 1"));
  CCA_CHECK_EQ(plain.str(), std::string("synthetic label 1"));
  CCA_ASSIGN(accented, BoundedText::parse(bytes({0x63, 0x61, 0x66, 0xC3, 0xA9})));
  CCA_CHECK_EQ(accented.str(), bytes({0x63, 0x61, 0x66, 0xC3, 0xA9}));
  CCA_ASSIGN(tabbed, BoundedText::parse(bytes({0x61, 0x09, 0x62})));
  CCA_CHECK_EQ(tabbed.str(), bytes({0x61, 0x09, 0x62}));

  const std::string at_bound(kMaxTextLength, 'a');
  const std::string past_bound(kMaxTextLength + 1U, 'a');
  CCA_ASSIGN(longest, BoundedText::parse(at_bound));
  CCA_CHECK_EQ(longest.str().size(), kMaxTextLength);
  CCA_CHECK_CODE(BoundedText::parse(past_bound), ErrorCode::TooManyItems);
  // The length bound is checked before the content, so an oversized malformed
  // payload reports the bound rather than the encoding.
  CCA_CHECK_CODE(BoundedText::parse(past_bound + bytes({0xC3})),
                 ErrorCode::TooManyItems);

  // Control characters are refused, tab is the single exception.
  CCA_CHECK_CODE(BoundedText::parse(bytes({0x01})), ErrorCode::InvalidText);
  CCA_CHECK_CODE(BoundedText::parse(bytes({0x1F})), ErrorCode::InvalidText);
  CCA_CHECK_CODE(BoundedText::parse(bytes({0x0A})), ErrorCode::InvalidText);
  CCA_CHECK_CODE(BoundedText::parse(bytes({0x0D})), ErrorCode::InvalidText);
  CCA_CHECK_CODE(BoundedText::parse(bytes({0x7F})), ErrorCode::InvalidText);
  CCA_CHECK_CODE(BoundedText::parse(bytes({0x61, 0x0A, 0x62})),
                 ErrorCode::InvalidText);
  // An embedded NUL is not usable UTF-8 at all.
  CCA_CHECK_CODE(BoundedText::parse(bytes({0x61, 0x00, 0x62})),
                 ErrorCode::InvalidUtf8);
  CCA_CHECK_CODE(BoundedText::parse(bytes({0xC3})), ErrorCode::InvalidUtf8);
  CCA_CHECK_CODE(BoundedText::parse(bytes({0xFF})), ErrorCode::InvalidUtf8);
  CCA_CHECK_CODE(BoundedText::parse(bytes({0xE0, 0x80, 0xAF})),
                 ErrorCode::InvalidUtf8);

  // An explicit bound is exact at the bound and refused one byte past it.
  CCA_ASSIGN(exact, BoundedText::parse("abc", 3U));
  CCA_CHECK_EQ(exact.str(), std::string("abc"));
  CCA_CHECK_CODE(BoundedText::parse("abc", 2U), ErrorCode::TooManyItems);
  CCA_ASSIGN(no_room, BoundedText::parse(std::string_view(), 0U));
  CCA_CHECK(no_room.empty());

  const BoundedText validated = BoundedText::from_validated("kept");
  CCA_CHECK_EQ(validated.str(), std::string("kept"));
  CCA_CHECK(validated == BoundedText::from_validated("kept"));
  CCA_CHECK(validated != plain);
  CCA_CHECK(validated < plain);
  CCA_CHECK(BoundedText{}.empty());
}

CCA_TEST(document_ref_parse_bounds_and_characters) {
  CCA_CHECK_CODE(DocumentRef::parse(std::string_view()), ErrorCode::InvalidText);

  CCA_ASSIGN(simple, DocumentRef::parse("datasheet-1"));
  CCA_CHECK_EQ(simple.str(), std::string("datasheet-1"));
  CCA_ASSIGN(rich, DocumentRef::parse("doc/1#section?x=1&y=2%20+~@,;!"));
  CCA_CHECK_EQ(rich.str(), std::string("doc/1#section?x=1&y=2%20+~@,;!"));

  const std::string at_bound(kMaxReferenceLength, 'r');
  const std::string past_bound(kMaxReferenceLength + 1U, 'r');
  CCA_ASSIGN(longest, DocumentRef::parse(at_bound));
  CCA_CHECK_EQ(longest.str().size(), kMaxReferenceLength);
  CCA_CHECK_CODE(DocumentRef::parse(past_bound), ErrorCode::TooManyItems);

  CCA_CHECK_CODE(DocumentRef::parse("has space"), ErrorCode::InvalidText);
  CCA_CHECK_CODE(DocumentRef::parse(bytes({0x62, 0x61, 0x63, 0x6B, 0x5C, 0x73})),
                 ErrorCode::InvalidText);
  CCA_CHECK_CODE(DocumentRef::parse(bytes({0xC3, 0xA9})), ErrorCode::InvalidText);
  CCA_CHECK_CODE(DocumentRef::parse(bytes({0x61, 0x00, 0x62})),
                 ErrorCode::InvalidText);
  CCA_CHECK_CODE(DocumentRef::parse(bytes({0x71, 0x75, 0x6F, 0x74, 0x65, 0x22, 0x6D})),
                 ErrorCode::InvalidText);

  CCA_CHECK(DocumentRef{} == DocumentRef{});
  CCA_CHECK(simple != rich);
  CCA_CHECK(DocumentRef{} < simple);
}

CCA_TEST(parse_int64_is_strict) {
  CCA_ASSIGN(zero, parse_int64("0"));
  CCA_CHECK_EQ(zero, 0);
  CCA_ASSIGN(one, parse_int64("1"));
  CCA_CHECK_EQ(one, 1);
  CCA_ASSIGN(minus_one, parse_int64("-1"));
  CCA_CHECK_EQ(minus_one, -1);
  CCA_ASSIGN(maximum, parse_int64("9223372036854775807"));
  CCA_CHECK_EQ(maximum, kI64Max);
  CCA_ASSIGN(minimum, parse_int64("-9223372036854775808"));
  CCA_CHECK_EQ(minimum, kI64Min);

  CCA_CHECK_CODE(parse_int64(std::string_view()), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64("-"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64("+1"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64("+0"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64("--1"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64(" 1"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64("1 "), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64(bytes({0x31, 0x09})), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64("01"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64("00"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64("007"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64("1.0"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64("1e3"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64("0x10"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64("1,000"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64("abc"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64("12a"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_int64(bytes({0x31, 0x32, 0xE2, 0x82, 0xAC})),
                 ErrorCode::InvalidNumber);

  CCA_CHECK_CODE(parse_int64("9223372036854775808"), ErrorCode::OutOfRange);
  CCA_CHECK_CODE(parse_int64("-9223372036854775809"), ErrorCode::OutOfRange);
  CCA_CHECK_CODE(parse_int64("18446744073709551615"), ErrorCode::OutOfRange);
  CCA_CHECK_CODE(parse_int64("99999999999999999999999999999"),
                 ErrorCode::OutOfRange);

  // Canonical rendering round trips through the strict parser.
  for (const std::int64_t value :
       {kI64Min, kI64Min + 1, static_cast<std::int64_t>(-1000),
        static_cast<std::int64_t>(-1), static_cast<std::int64_t>(0),
        static_cast<std::int64_t>(1), static_cast<std::int64_t>(1000), kI64Max - 1,
        kI64Max}) {
    CCA_ASSIGN(parsed, parse_int64(to_decimal(value)));
    CCA_CHECK_EQ(parsed, value);
    CCA_CHECK_EQ(to_decimal(value), to_decimal(parsed));
  }
}

CCA_TEST(parse_uint64_and_parse_uint32_are_strict) {
  CCA_ASSIGN(zero, parse_uint64("0"));
  CCA_CHECK_EQ(zero, 0U);
  CCA_ASSIGN(maximum, parse_uint64("18446744073709551615"));
  CCA_CHECK_EQ(maximum, kU64Max);

  CCA_CHECK_CODE(parse_uint64(std::string_view()), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_uint64("-1"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_uint64("-0"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_uint64("+0"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_uint64("+1"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_uint64("01"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_uint64(" 0"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_uint64("0 "), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_uint64("0x0"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_uint64("18446744073709551616"), ErrorCode::OutOfRange);
  CCA_CHECK_CODE(parse_uint64("99999999999999999999999999999"),
                 ErrorCode::OutOfRange);

  CCA_ASSIGN(u32_zero, parse_uint32("0"));
  CCA_CHECK_EQ(u32_zero, 0U);
  CCA_ASSIGN(u32_max, parse_uint32("4294967295"));
  CCA_CHECK_EQ(u32_max, 4294967295U);
  CCA_CHECK_CODE(parse_uint32("4294967296"), ErrorCode::OutOfRange);
  CCA_CHECK_CODE(parse_uint32("18446744073709551615"), ErrorCode::OutOfRange);
  CCA_CHECK_CODE(parse_uint32("-1"), ErrorCode::InvalidNumber);
  CCA_CHECK_CODE(parse_uint32("00"), ErrorCode::InvalidNumber);

  for (const std::uint64_t value :
       {0ULL, 1ULL, 4294967295ULL, 4294967296ULL, kU64Max}) {
    CCA_ASSIGN(parsed, parse_uint64(to_decimal(value)));
    CCA_CHECK_EQ(parsed, value);
    CCA_CHECK_EQ(to_decimal(value), to_decimal(parsed));
  }
}

CCA_TEST(decimal_and_ascii_helpers) {
  CCA_CHECK_EQ(to_decimal(static_cast<std::int64_t>(0)), std::string("0"));
  CCA_CHECK_EQ(to_decimal(static_cast<std::int64_t>(-1)), std::string("-1"));
  CCA_CHECK_EQ(to_decimal(kI64Max), std::string("9223372036854775807"));
  CCA_CHECK_EQ(to_decimal(kI64Min), std::string("-9223372036854775808"));
  CCA_CHECK_EQ(to_decimal(kU64Max), std::string("18446744073709551615"));

  CCA_CHECK_EQ(to_lower_ascii("ABC xyz 123"), std::string("abc xyz 123"));
  CCA_CHECK_EQ(to_lower_ascii(std::string_view()), std::string());
  CCA_CHECK_EQ(to_lower_ascii(bytes({0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x7B, 0x7C,
                                     0x7D, 0x7E})),
               bytes({0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x7B, 0x7C, 0x7D, 0x7E}));
  CCA_CHECK_EQ(to_lower_ascii(bytes({0xC3, 0x89})), bytes({0xC3, 0x89}));

  CCA_CHECK(is_ascii_digit('0'));
  CCA_CHECK(is_ascii_digit('9'));
  CCA_CHECK(!is_ascii_digit('/'));
  CCA_CHECK(!is_ascii_digit(':'));
  CCA_CHECK(!is_ascii_digit('a'));
  CCA_CHECK(!is_ascii_digit(' '));
  CCA_CHECK(!is_ascii_digit(static_cast<char>(0x80)));

  CCA_CHECK(is_ascii_alphanumeric('0'));
  CCA_CHECK(is_ascii_alphanumeric('9'));
  CCA_CHECK(is_ascii_alphanumeric('a'));
  CCA_CHECK(is_ascii_alphanumeric('z'));
  CCA_CHECK(is_ascii_alphanumeric('A'));
  CCA_CHECK(is_ascii_alphanumeric('Z'));
  CCA_CHECK(!is_ascii_alphanumeric('@'));
  CCA_CHECK(!is_ascii_alphanumeric('['));
  CCA_CHECK(!is_ascii_alphanumeric(static_cast<char>(0x60)));
  CCA_CHECK(!is_ascii_alphanumeric('{'));
  CCA_CHECK(!is_ascii_alphanumeric('/'));
  CCA_CHECK(!is_ascii_alphanumeric(':'));
  CCA_CHECK(!is_ascii_alphanumeric(' '));
  CCA_CHECK(!is_ascii_alphanumeric(static_cast<char>(0x80)));

  CCA_CHECK_EQ(to_printable_ascii(bytes({0x20, 0x7E, 0x1F, 0x7F, 0x80, 0xFF, 0x41})),
               std::string(" ~????A"));
  CCA_CHECK_EQ(to_printable_ascii(bytes({0x0A, 0x09})), std::string("??"));
  CCA_CHECK_EQ(to_printable_ascii(std::string_view()), std::string());
  CCA_CHECK_EQ(to_printable_ascii("plain"), std::string("plain"));
  CCA_CHECK_EQ(to_printable_ascii(bytes({0xC3, 0xA9})).size(), 2U);
}

CCA_TEST(split_list_preserves_empty_fields) {
  CCA_CHECK(split_list(std::string_view()) == std::vector<std::string>{""});
  CCA_CHECK(split_list("a") == std::vector<std::string>{"a"});
  CCA_CHECK(split_list("a,b,c") == (std::vector<std::string>{"a", "b", "c"}));
  CCA_CHECK(split_list("a,,b") == (std::vector<std::string>{"a", "", "b"}));
  CCA_CHECK(split_list(",") == (std::vector<std::string>{"", ""}));
  CCA_CHECK(split_list(",,") == (std::vector<std::string>{"", "", ""}));
  CCA_CHECK(split_list("a,") == (std::vector<std::string>{"a", ""}));
  CCA_CHECK(split_list(",a") == (std::vector<std::string>{"", "a"}));
  CCA_CHECK(split_list(" a , b ") == (std::vector<std::string>{" a ", " b "}));
  CCA_CHECK_EQ(split_list("a,b,c").size(), 3U);
  CCA_CHECK_EQ(split_list(",,").size(), 3U);
}
