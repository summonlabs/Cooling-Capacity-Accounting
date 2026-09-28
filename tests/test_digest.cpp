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

// Proof obligations for SHA-256 and the digest text form. The known answers are
// the published FIPS 180-4 / NIST vectors and independently computed digests of
// messages that exercise every padding boundary (length modulo 64 of 0, 48, 55,
// 56, 63 and 1). Streaming updates split at every boundary must reproduce the
// one-shot digest exactly.

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

namespace {

using cooling_capacity_accounting::Digest;
using cooling_capacity_accounting::DigestBuilder;
using cooling_capacity_accounting::ErrorCode;
using cooling_capacity_accounting::Sha256;

constexpr std::uint64_t kU64Max = std::numeric_limits<std::uint64_t>::max();

/// The 112-byte message of the FIPS 180-4 example set.
constexpr std::string_view kMessage112 =
    "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
    "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
/// Its first 64 bytes: the exact multiple-of-64 padding boundary.
constexpr std::string_view kMessage64 =
    "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno";
/// The 56-byte message of the FIPS 180-4 example set: 56 modulo 64 needs the
/// second padding block.
constexpr std::string_view kMessage56 =
    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

struct Vector {
  std::string_view message;
  std::string_view hex;
};

const Vector kVectors[] = {
    {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
    // 55 bytes: one byte short of the first padding boundary.
    {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklm",
     "4243974b4dd5dcbe9952db216e4e399d1d1a21d0bc15d6197aa93a12136cef55"},
    {kMessage56, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    // 63 bytes: one byte short of a full block.
    {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmn",
     "6e406c4796591ba9868fe98f1c8201e06c6d8b55d273f17fdd957d1288a31d85"},
    {kMessage64, "2ff100b36c386c65a1afc462ad53e25479bec9498ed00aa5a04de584bc25301b"},
    // 65 bytes: one byte past a full block.
    {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoo",
     "df35c59d0fa6c8bbdc78a95abdad3edc60b348d578cfcdb3483196f5e429f325"},
    {kMessage112,
     "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
    // 119 bytes: 55 modulo 64 with the second block required.
    {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
     "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstuabcdefg",
     "05bb9ee2e23c7acd34dfc2205f7d2b40895aca97cb28db7e40c6086361601702"},
    // 128 bytes: two full blocks, so padding needs a third.
    {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
     "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstuabcdefghijklmnop",
     "f9871838691aec98d224610a76d485552f9d937df321999e4aeecf51c10056a9"},
};

/// A 200-byte message containing every byte value from 0 to 199, including NUL:
/// the digest must be computed over bytes, not over text.
[[nodiscard]] std::string byte_sequence_message() {
  std::string message;
  message.reserve(200U);
  for (int value = 0; value < 200; ++value) {
    message.push_back(static_cast<char>(static_cast<unsigned char>(value)));
  }
  return message;
}

constexpr std::string_view kByteSequenceHex =
    "1901da1c9f699b48f6b2636e65cbf73abf99d0441ef67f5c540a42f7051dec6f";

}  // namespace

CCA_TEST(sha256_matches_the_published_vectors) {
  for (const Vector& vector : kVectors) {
    const Digest observed = Digest::of_text(vector.message);
    if (observed.to_hex() != vector.hex) {
      CCA_FAIL(std::string("digest of a ") +
               std::to_string(vector.message.size()) +
               "-byte message does not match its published value: " +
               observed.to_hex());
      return;
    }
    CCA_CHECK_EQ(observed.to_hex().size(), 64U);
  }
  CCA_CHECK_EQ(std::size(kVectors), 10U);
}

CCA_TEST(sha256_hashes_binary_messages_including_nul_bytes) {
  const std::string message = byte_sequence_message();
  CCA_CHECK_EQ(message.size(), 200U);
  CCA_CHECK_EQ(message[0], static_cast<char>(0));
  CCA_CHECK_EQ(Digest::of_text(message).to_hex(), std::string(kByteSequenceHex));
  CCA_CHECK_EQ(Digest::of_bytes(message.data(), message.size()).to_hex(),
               std::string(kByteSequenceHex));

  const std::string zeros(64U, static_cast<char>(0));
  CCA_CHECK_EQ(Digest::of_text(zeros).to_hex(),
               std::string("f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b"));
}

CCA_TEST(sha256_of_a_long_message) {
  const std::string million(1'000'000U, 'a');
  CCA_CHECK_EQ(Digest::of_text(million).to_hex(),
               std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e"
                           "046d39ccc7112cd0"));

  // One byte more is a different digest, so length is really mixed in.
  const std::string million_and_one(1'000'001U, 'a');
  CCA_CHECK(Digest::of_text(million_and_one) != Digest::of_text(million));
}

CCA_TEST(streaming_updates_at_every_split_match_one_shot) {
  const std::string message = byte_sequence_message();
  const Digest expected = Digest::of_text(message);
  for (std::size_t split = 0; split <= message.size(); ++split) {
    Sha256 hasher;
    hasher.update(message.data(), split);
    hasher.update(message.data() + split, message.size() - split);
    const Digest observed = hasher.finish();
    if (observed != expected) {
      CCA_FAIL("a two-part update split at byte " + std::to_string(split) +
               " does not match the one-shot digest");
      return;
    }
  }
  CCA_CHECK_EQ(Digest::of_text(message), expected);
}

CCA_TEST(streaming_with_block_sized_chunks_matches_one_shot) {
  std::string message(kMessage112);
  message.append(200U, 'x');
  CCA_CHECK_EQ(message.size(), 312U);
  const Digest expected = Digest::of_text(message);

  for (const std::size_t chunk :
       {1U, 7U, 32U, 55U, 56U, 63U, 64U, 65U, 127U, 128U, 200U, 311U, 312U}) {
    Sha256 hasher;
    for (std::size_t offset = 0; offset < message.size(); offset += chunk) {
      const std::size_t remaining = message.size() - offset;
      const std::size_t take = remaining < chunk ? remaining : chunk;
      hasher.update(message.data() + offset, take);
    }
    if (hasher.finish() != expected) {
      CCA_FAIL("chunk size " + std::to_string(chunk) +
               " does not reproduce the one-shot digest");
      return;
    }
  }

  // One byte at a time, including the NUL byte of the sequence message.
  const std::string sequence = byte_sequence_message();
  Sha256 single;
  for (const char byte : sequence) {
    single.update(&byte, 1U);
  }
  CCA_CHECK_EQ(single.finish(), Digest::of_text(sequence));
}

CCA_TEST(sha256_reset_restarts_the_stream) {
  Sha256 hasher;
  hasher.update(std::string_view("abc"));
  const Digest first = hasher.finish();
  CCA_CHECK_EQ(first, Digest::of_text("abc"));

  // After reset the hasher is reusable and produces the identical digest.
  hasher.reset();
  hasher.update(std::string_view("abc"));
  CCA_CHECK_EQ(hasher.finish(), first);

  hasher.reset();
  hasher.update(std::string_view("ab"));
  hasher.update(std::string_view("c"));
  CCA_CHECK_EQ(hasher.finish(), first);

  Sha256 fresh;
  fresh.update(kMessage112);
  CCA_CHECK_EQ(fresh.finish(), Digest::of_text(kMessage112));
}

CCA_TEST(digest_parse_hex_accepts_exactly_sixty_four_hexadecimal_characters) {
  CCA_ASSIGN(lower, Digest::parse_hex(
                        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
  CCA_CHECK_EQ(lower.to_hex(),
               std::string("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
  CCA_ASSIGN(upper, Digest::parse_hex(
                        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"));
  CCA_CHECK_EQ(upper, lower);
  CCA_CHECK_EQ(upper.to_hex(), lower.to_hex());

  CCA_ASSIGN(mixed, Digest::parse_hex(
                        "0123456789aBcDeF0123456789AbCdEf0123456789aBcDeF0123456789AbCdEf"));
  CCA_CHECK_EQ(mixed, lower);

  const std::string at_bound(64U, 'f');
  CCA_ASSIGN(all_f, Digest::parse_hex(at_bound));
  CCA_CHECK_EQ(all_f.to_hex(), at_bound);

  // Everything that is not exactly 64 hexadecimal characters is refused.
  CCA_CHECK_CODE(Digest::parse_hex(std::string_view()), ErrorCode::InvalidDigestText);
  CCA_CHECK_CODE(Digest::parse_hex(std::string(63U, 'a')),
                 ErrorCode::InvalidDigestText);
  CCA_CHECK_CODE(Digest::parse_hex(std::string(65U, 'a')),
                 ErrorCode::InvalidDigestText);
  CCA_CHECK_CODE(Digest::parse_hex(std::string(66U, 'a')),
                 ErrorCode::InvalidDigestText);
  CCA_CHECK_CODE(Digest::parse_hex(std::string(64U, 'g')),
                 ErrorCode::InvalidDigestText);
  CCA_CHECK_CODE(Digest::parse_hex(std::string(64U, 'G')),
                 ErrorCode::InvalidDigestText);
  CCA_CHECK_CODE(Digest::parse_hex(std::string(64U, ' ')),
                 ErrorCode::InvalidDigestText);
  CCA_CHECK_CODE(Digest::parse_hex(std::string(64U, '0') + "x"),
                 ErrorCode::InvalidDigestText);
  CCA_CHECK_CODE(Digest::parse_hex("-123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"),
                 ErrorCode::InvalidDigestText);
}

CCA_TEST(digest_to_hex_bytes_and_zero) {
  const Digest empty = Digest::of_text(std::string_view());
  CCA_CHECK(!empty.is_zero());
  CCA_CHECK_EQ(empty.to_hex().size(), 64U);
  CCA_CHECK_EQ(empty.to_hex(),
               std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

  // to_hex is lower case, and parsing it back reproduces the same bytes.
  for (const std::string_view message :
       {std::string_view(), std::string_view("abc"), std::string_view("attempt-1"),
        kMessage112}) {
    const Digest digest = Digest::of_text(message);
    for (const char byte : digest.to_hex()) {
      const bool lower_hexadecimal = (byte >= '0' && byte <= '9') ||
                                     (byte >= 'a' && byte <= 'f');
      if (!lower_hexadecimal) {
        CCA_FAIL("to_hex produced a byte that is not lower-case hexadecimal");
        return;
      }
    }
    CCA_ASSIGN(parsed, Digest::parse_hex(digest.to_hex()));
    CCA_CHECK_EQ(parsed, digest);
    CCA_CHECK_EQ(parsed.bytes(), digest.bytes());
  }

  // The zero digest is all zeroes and nothing else is.
  const Digest zero = Digest::zero();
  CCA_CHECK(zero.is_zero());
  CCA_CHECK_EQ(zero.to_hex(), std::string(64U, '0'));
  CCA_ASSIGN(parsed_zero, Digest::parse_hex(std::string(64U, '0')));
  CCA_CHECK(parsed_zero.is_zero());
  CCA_CHECK_EQ(parsed_zero, zero);

  std::array<std::uint8_t, Digest::kSize> bytes{};
  bytes.fill(0U);
  CCA_CHECK(Digest::from_bytes(bytes).is_zero());
  CCA_CHECK_EQ(Digest::from_bytes(bytes), zero);
  bytes[0] = 1U;
  CCA_CHECK(!Digest::from_bytes(bytes).is_zero());
  bytes[0] = 0U;
  bytes[Digest::kSize - 1U] = 1U;
  CCA_CHECK(!Digest::from_bytes(bytes).is_zero());
  CCA_CHECK(Digest::from_bytes(bytes) != zero);

  // Byte-level equality and ordering follow the digest bytes.
  CCA_CHECK(Digest::of_text("a") == Digest::of_text("a"));
  CCA_CHECK(Digest::of_text("a") != Digest::of_text("b"));
  const Digest low = Digest::of_text("a");
  const Digest high = Digest::of_text("b");
  CCA_CHECK((low < high) != (high < low));
  CCA_CHECK(!(low < low));
  CCA_CHECK(zero < Digest::of_text("a") || Digest::of_text("a") < zero);
}

CCA_TEST(digest_builder_is_deterministic_and_finish_does_not_consume) {
  DigestBuilder builder;
  builder.add_text("alpha");
  const Digest first = builder.finish();
  CCA_CHECK_EQ(builder.finish(), first);
  CCA_CHECK_EQ(builder.finish(), first);

  builder.add_text("beta");
  const Digest extended = builder.finish();
  CCA_CHECK(extended != first);
  CCA_CHECK_EQ(builder.finish(), extended);

  DigestBuilder repeated;
  repeated.add_text("alpha");
  CCA_CHECK_EQ(repeated.finish(), first);

  // An empty builder is the digest of the empty input.
  const DigestBuilder empty;
  CCA_CHECK_EQ(empty.finish(), Digest::of_text(std::string_view()));

  // A builder with one field is not the raw digest of that field's bytes: the
  // field is length prefixed.
  DigestBuilder built_field;
  built_field.add_text("abc");
  CCA_CHECK(built_field.finish() != Digest::of_text("abc"));
}

CCA_TEST(digest_builder_separates_fields_of_the_same_concatenation) {
  DigestBuilder left;
  left.add_text("ab");
  left.add_text("c");
  DigestBuilder right;
  right.add_text("a");
  right.add_text("bc");
  DigestBuilder joined;
  joined.add_text("abc");
  CCA_CHECK(left.finish() != right.finish());
  CCA_CHECK(left.finish() != joined.finish());
  CCA_CHECK(right.finish() != joined.finish());

  // An empty field is still a field.
  DigestBuilder empty_then_a;
  empty_then_a.add_text(std::string_view());
  empty_then_a.add_text("a");
  DigestBuilder a_only;
  a_only.add_text("a");
  CCA_CHECK(empty_then_a.finish() != a_only.finish());

  // A NUL byte is not the absence of a byte.
  DigestBuilder nul;
  nul.add_text(std::string(1U, static_cast<char>(0)));
  CCA_CHECK(nul.finish() != empty_then_a.finish());
  CCA_CHECK(nul.finish() != a_only.finish());

  // Bytes and text agree when they carry the same bytes.
  const std::string blob = byte_sequence_message();
  DigestBuilder via_bytes;
  via_bytes.add_bytes(blob.data(), blob.size());
  DigestBuilder via_text;
  via_text.add_text(blob);
  CCA_CHECK_EQ(via_bytes.finish(), via_text.finish());
  CCA_CHECK(via_bytes.finish() != Digest::of_text(blob));

  // Order is part of the mixed sequence.
  DigestBuilder forward;
  forward.add_text("a");
  forward.add_text("b");
  DigestBuilder backward;
  backward.add_text("b");
  backward.add_text("a");
  CCA_CHECK(forward.finish() != backward.finish());
}

CCA_TEST(digest_builder_scalar_and_named_field_encodings) {
  DigestBuilder as_byte;
  as_byte.add_u8(1U);
  DigestBuilder as_u32;
  as_u32.add_u32(1U);
  DigestBuilder as_u64;
  as_u64.add_u64(1U);
  CCA_CHECK(as_byte.finish() != as_u32.finish());
  CCA_CHECK(as_u32.finish() != as_u64.finish());
  CCA_CHECK(as_byte.finish() != as_u64.finish());

  DigestBuilder as_zero_byte;
  as_zero_byte.add_u8(0U);
  DigestBuilder as_zero_u64;
  as_zero_u64.add_u64(0U);
  CCA_CHECK(as_zero_byte.finish() != as_zero_u64.finish());

  // A boolean is one byte, and the two boolean values differ.
  DigestBuilder true_flag;
  true_flag.add_bool(true);
  DigestBuilder false_flag;
  false_flag.add_bool(false);
  CCA_CHECK_EQ(true_flag.finish(), as_byte.finish());
  CCA_CHECK(true_flag.finish() != false_flag.finish());

  // A signed value is mixed as its two's complement bytes.
  DigestBuilder negative;
  negative.add_i64(-1);
  DigestBuilder maximum;
  maximum.add_u64(kU64Max);
  CCA_CHECK_EQ(negative.finish(), maximum.finish());

  // A named field is the tag mixed before the value.
  DigestBuilder named;
  named.add_field("k", "v");
  DigestBuilder split;
  split.add_text("k");
  split.add_text("v");
  CCA_CHECK_EQ(named.finish(), split.finish());
  DigestBuilder other_split;
  other_split.add_field("kv", std::string_view());
  CCA_CHECK(named.finish() != other_split.finish());

  DigestBuilder value_u64;
  value_u64.add_field_u64("k", 1U);
  DigestBuilder tag_then_u64;
  tag_then_u64.add_text("k");
  tag_then_u64.add_u64(1U);
  CCA_CHECK_EQ(value_u64.finish(), tag_then_u64.finish());

  DigestBuilder value_i64;
  value_i64.add_field_i64("k", 1);
  CCA_CHECK_EQ(value_i64.finish(), value_u64.finish());
  DigestBuilder value_i64_negative;
  value_i64_negative.add_field_i64("k", -1);
  CCA_CHECK(value_i64_negative.finish() != value_u64.finish());

  // A section marker is not the section name as a plain field.
  DigestBuilder sectioned;
  sectioned.add_section("body");
  sectioned.add_text("a");
  DigestBuilder plain;
  plain.add_text("body");
  plain.add_text("a");
  CCA_CHECK(sectioned.finish() != plain.finish());
  DigestBuilder other_section;
  other_section.add_section("other");
  other_section.add_text("a");
  CCA_CHECK(sectioned.finish() != other_section.finish());
  DigestBuilder same_section;
  same_section.add_section("body");
  same_section.add_text("a");
  CCA_CHECK_EQ(same_section.finish(), sectioned.finish());
}
