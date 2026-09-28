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

// Deterministic sabotage of a real store directory: every corruption is made
// with std::filesystem and std::FILE, and the store is then reopened and asked
// for its exact outcome.

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "fixtures.hpp"
#include "test_harness.hpp"

using namespace cooling_capacity_accounting;
using cca_test::TempDir;
using cca_test::describe_error;
using cca_test::fixture_now;

namespace {

constexpr std::string_view kManifestName = "MANIFEST";

// ---------------------------------------------------------------------------
// Real files
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
  std::vector<std::uint8_t> bytes;
  std::FILE* file = nullptr;
#ifdef _WIN32
  if (_wfopen_s(&file, path.wstring().c_str(), L"rb") != 0) {
    file = nullptr;
  }
#else
  file = std::fopen(path.string().c_str(), "rb");
#endif
  if (file == nullptr) {
    return bytes;
  }
  std::uint8_t buffer[4096];
  std::size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    bytes.insert(bytes.end(), buffer, buffer + read);
  }
  static_cast<void>(std::fclose(file));
  return bytes;
}

[[nodiscard]] bool write_bytes(const std::filesystem::path& path,
                               const std::vector<std::uint8_t>& bytes) {
  std::FILE* file = nullptr;
#ifdef _WIN32
  if (_wfopen_s(&file, path.wstring().c_str(), L"wb") != 0) {
    file = nullptr;
  }
#else
  file = std::fopen(path.string().c_str(), "wb");
#endif
  if (file == nullptr) {
    return false;
  }
  const std::size_t written =
      bytes.empty() ? 0U : std::fwrite(bytes.data(), 1, bytes.size(), file);
  const int closed = std::fclose(file);
  return written == bytes.size() && closed == 0;
}

/// Keeps the first length bytes of the file and drops the rest.
[[nodiscard]] bool truncate_to(const std::filesystem::path& path, std::size_t length) {
  const std::vector<std::uint8_t> bytes = read_bytes(path);
  if (bytes.size() < length) {
    return false;
  }
  return write_bytes(path,
                     std::vector<std::uint8_t>(
                         bytes.begin(),
                         bytes.begin() + static_cast<std::ptrdiff_t>(length)));
}

[[nodiscard]] bool flip_byte(const std::filesystem::path& path, std::size_t offset) {
  std::vector<std::uint8_t> bytes = read_bytes(path);
  if (offset >= bytes.size()) {
    return false;
  }
  bytes[offset] = static_cast<std::uint8_t>(bytes[offset] ^ 0x01U);
  return write_bytes(path, bytes);
}

[[nodiscard]] std::vector<std::filesystem::path> files_with_prefix(
    const std::filesystem::path& root, std::string_view prefix) {
  std::vector<std::filesystem::path> found;
  std::error_code code;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(root, code)) {
    const std::string name = entry.path().filename().string();
    if (name.rfind(prefix, 0) == 0) {
      found.push_back(entry.path());
    }
  }
  std::sort(found.begin(), found.end());
  return found;
}

/// The generation file the manifest names: the highest generation present.
[[nodiscard]] std::filesystem::path current_generation_file(
    const std::filesystem::path& root) {
  const std::vector<std::filesystem::path> files = files_with_prefix(root, "gen-");
  if (files.empty()) {
    CCA_FAIL("no generation file was found in the store directory");
    return root;
  }
  return files.back();
}

[[nodiscard]] std::string generation_name(std::uint64_t value) {
  std::string digits = to_decimal(value);
  while (digits.size() < 20U) {
    digits.insert(digits.begin(), '0');
  }
  return "gen-" + digits + ".cca";
}

// ---------------------------------------------------------------------------
// Publication through the public API
// ---------------------------------------------------------------------------

[[nodiscard]] Result<AccountingSnapshot> facility_snapshot(std::size_t units_per_loop,
                                                           std::uint64_t revision) {
  cca_test::FacilitySpec spec;
  spec.zones = 2;
  spec.loops_per_zone = 2;
  spec.units_per_loop = units_per_loop;
  AccountingInput input = cca_test::make_facility(spec);
  CCA_TRY_ASSIGN(revision_value, StateRevision::of(revision));
  input.generations.revision = revision_value;
  return AccountingSnapshot::create(std::move(input), fixture_now());
}

/// Publishes count generations into the store at root and closes it.
[[nodiscard]] Result<void> publish_generations(const std::filesystem::path& root,
                                              std::size_t count) {
  StoreOptions options;
  options.root = root;
  CCA_TRY_ASSIGN(store, AccountingStore::open(options));
  for (std::size_t index = 0; index < count; ++index) {
    CCA_TRY_ASSIGN(snapshot, facility_snapshot(3U + index, index));
    CCA_TRY_ASSIGN(attempt,
                   AttemptId::from_material("corruption-" + std::to_string(index)));
    PublishRequest request;
    request.attempt = attempt;
    request.fingerprint =
        Digest::of_text("corruption-fingerprint-" + std::to_string(index));
    request.expected_previous = store.current_generation();
    request.expected_revision = snapshot.header().generations.revision;
    request.epoch = options.epoch;
    request.published_at = Timestamp::of_unix_milliseconds(
        fixture_now().unix_milliseconds() + static_cast<std::int64_t>(index));
    CCA_TRY(store.publish(snapshot, request));
  }
  return store.close();
}

/// Opens the store at root and requires the exact error code.
void expect_open_refused(const std::filesystem::path& root, ErrorCode expected,
                         const char* what) {
  StoreOptions options;
  options.root = root;
  Result<AccountingStore> opened = AccountingStore::open(options);
  if (opened.ok()) {
    CCA_FAIL(std::string(what) + ": the store opened although it should have been "
                                 "refused with " +
             std::string(error_code_name(expected)));
    const Result<void> closed = opened.value().close();
    static_cast<void>(closed);
    return;
  }
  if (opened.error().code() != expected) {
    CCA_FAIL(std::string(what) + ": expected " + std::string(error_code_name(expected)) +
             " but got " + describe_error(opened.error()));
  }
}

/// Requires that the store resolves to exactly one whole authoritative
/// generation, that it is the expected one, and that every retained record
/// verifies.
void expect_single_authoritative(const std::filesystem::path& root,
                                 std::uint64_t expected_generation, const char* what) {
  StoreOptions options;
  options.root = root;
  Result<AccountingStore> store = AccountingStore::open(options);
  if (!store.ok()) {
    CCA_FAIL(std::string(what) + ": the store did not open: " +
             describe_error(store.error()));
    return;
  }
  if (!store.value().has_generation()) {
    CCA_FAIL(std::string(what) + ": the store reports no generation");
    CCA_REQUIRE_OK(store.value().close());
    return;
  }
  if (store.value().current_generation().value() != expected_generation) {
    CCA_FAIL(std::string(what) + ": expected generation " +
             to_decimal(expected_generation) + " but the store reports " +
             to_decimal(store.value().current_generation().value()));
  }
  Result<AccountingSnapshot> latest = store.value().latest();
  if (!latest.ok()) {
    CCA_FAIL(std::string(what) + ": latest() failed: " + describe_error(latest.error()));
  } else {
    CCA_REQUIRE_OK(latest.value().verify());
    CCA_CHECK(latest.value().ledger().verify_closure().exact);
  }
  Result<std::vector<StoredGeneration>> history = store.value().history();
  if (!history.ok()) {
    CCA_FAIL(std::string(what) + ": history() failed: " +
             describe_error(history.error()));
    CCA_REQUIRE_OK(store.value().close());
    return;
  }
  for (std::size_t index = 0; index < history.value().size(); ++index) {
    if (index > 0U &&
        !(history.value()[index - 1U].generation < history.value()[index].generation)) {
      CCA_FAIL(std::string(what) + ": history is not strictly ascending");
    }
    Result<AccountingSnapshot> loaded =
        store.value().load(history.value()[index].generation);
    if (!loaded.ok()) {
      CCA_FAIL(std::string(what) + ": a retained generation did not load: " +
               describe_error(loaded.error()));
      continue;
    }
    CCA_REQUIRE_OK(loaded.value().verify());
  }
  const Result<void> closed = store.value().close();
  CCA_CHECK(closed.ok());
}

}  // namespace

// ---------------------------------------------------------------------------
// A generation file that was tampered with
// ---------------------------------------------------------------------------

CCA_TEST(corruption_truncated_generation_file_is_refused) {
  TempDir dir("corruption-truncate-generation");
  const std::filesystem::path root = dir.path() / "store";
  CCA_REQUIRE_OK(publish_generations(root, 3));
  expect_single_authoritative(root, 3, "before any sabotage");

  const std::filesystem::path generation = current_generation_file(root);
  const std::vector<std::uint8_t> original = read_bytes(generation);
  CCA_CHECK(original.size() > 64U);

  const std::vector<std::size_t> lengths{0U,
                                         1U,
                                         8U,
                                         16U,
                                         24U,
                                         original.size() / 2U,
                                         original.size() - 33U,
                                         original.size() - 32U,
                                         original.size() - 1U};
  for (const std::size_t length : lengths) {
    // Every iteration starts from the original bytes: truncation is not
    // cumulative, or the second length would be measuring the first one.
    CCA_CHECK(write_bytes(generation, original));
    CCA_CHECK(truncate_to(generation, length));
    expect_open_refused(root, ErrorCode::TruncatedInput,
                        "a truncated generation file");
  }

  // Restoring the bytes restores the store exactly.
  CCA_CHECK(write_bytes(generation, original));
  expect_single_authoritative(root, 3, "after restoring the generation file");
}

CCA_TEST(corruption_flipped_generation_byte_is_refused) {
  TempDir dir("corruption-flip-generation");
  const std::filesystem::path root = dir.path() / "store";
  CCA_REQUIRE_OK(publish_generations(root, 2));
  expect_single_authoritative(root, 2, "before any sabotage");

  const std::filesystem::path generation = current_generation_file(root);
  const std::vector<std::uint8_t> original = read_bytes(generation);
  CCA_CHECK(original.size() > 64U);

  // The last byte before the file digest is inside the payload.
  CCA_CHECK(flip_byte(generation, original.size() - 33U));
  expect_open_refused(root, ErrorCode::IntegrityFailure, "a flipped payload byte");

  CCA_CHECK(write_bytes(generation, original));
  expect_single_authoritative(root, 2, "after restoring the payload");

  // A flipped digest byte is refused the same way.
  CCA_CHECK(flip_byte(generation, original.size() - 1U));
  expect_open_refused(root, ErrorCode::IntegrityFailure, "a flipped file digest byte");

  CCA_CHECK(write_bytes(generation, original));
  expect_single_authoritative(root, 2, "after restoring the file digest");
}

// ---------------------------------------------------------------------------
// The manifest
// ---------------------------------------------------------------------------

CCA_TEST(corruption_truncated_manifest_is_refused) {
  TempDir dir("corruption-truncate-manifest");
  const std::filesystem::path root = dir.path() / "store";
  CCA_REQUIRE_OK(publish_generations(root, 2));
  expect_single_authoritative(root, 2, "before any sabotage");

  const std::filesystem::path manifest = root / std::filesystem::path(kManifestName);
  CCA_CHECK(std::filesystem::is_regular_file(manifest));
  const std::vector<std::uint8_t> original = read_bytes(manifest);
  CCA_CHECK(original.size() > 64U);

  const std::vector<std::size_t> lengths{0U,
                                         1U,
                                         8U,
                                         12U,
                                         16U,
                                         32U,
                                         48U,
                                         original.size() / 2U,
                                         original.size() - 32U,
                                         original.size() - 1U};
  for (const std::size_t length : lengths) {
    // Every iteration starts from the original bytes: truncation is not
    // cumulative.
    CCA_CHECK(write_bytes(manifest, original));
    CCA_CHECK(truncate_to(manifest, length));
    expect_open_refused(root, ErrorCode::TruncatedInput, "a truncated manifest");
  }

  CCA_CHECK(write_bytes(manifest, original));
  expect_single_authoritative(root, 2, "after restoring the manifest");
}

CCA_TEST(corruption_flipped_manifest_byte_is_refused) {
  TempDir dir("corruption-flip-manifest");
  const std::filesystem::path root = dir.path() / "store";
  CCA_REQUIRE_OK(publish_generations(root, 2));
  const std::filesystem::path manifest = root / std::filesystem::path(kManifestName);
  const std::vector<std::uint8_t> original = read_bytes(manifest);
  CCA_CHECK(original.size() > 64U);

  // A byte inside the covered region changes the manifest digest.
  CCA_CHECK(flip_byte(manifest, 20U));
  expect_open_refused(root, ErrorCode::IntegrityFailure, "a flipped manifest byte");

  CCA_CHECK(write_bytes(manifest, original));
  expect_single_authoritative(root, 2, "after restoring the manifest");

  // The trailing digest is covered as well.
  CCA_CHECK(flip_byte(manifest, original.size() - 1U));
  expect_open_refused(root, ErrorCode::IntegrityFailure, "a flipped manifest digest");

  CCA_CHECK(write_bytes(manifest, original));
  // The reserved field is checked before the digest.
  CCA_CHECK(flip_byte(manifest, 12U));
  expect_open_refused(root, ErrorCode::ReservedFieldNotZero,
                      "a non-zero manifest reserved field");

  CCA_CHECK(write_bytes(manifest, original));
  // The magic is checked first of all.
  CCA_CHECK(flip_byte(manifest, 0U));
  expect_open_refused(root, ErrorCode::MalformedRecord, "a wrong manifest magic");

  CCA_CHECK(write_bytes(manifest, original));
  expect_single_authoritative(root, 2, "after restoring the manifest again");
}

CCA_TEST(corruption_missing_generation_file_is_refused) {
  TempDir dir("corruption-missing-generation");
  const std::filesystem::path root = dir.path() / "store";
  CCA_REQUIRE_OK(publish_generations(root, 2));
  expect_single_authoritative(root, 2, "before any sabotage");

  const std::filesystem::path generation = current_generation_file(root);
  const std::vector<std::uint8_t> original = read_bytes(generation);
  CCA_CHECK(!original.empty());
  std::error_code code;
  CCA_CHECK(std::filesystem::remove(generation, code));
  CCA_CHECK(!code);
  CCA_CHECK(!std::filesystem::exists(generation));

  // The manifest names a generation that is not there.
  expect_open_refused(root, ErrorCode::IntegrityFailure,
                      "a generation file the manifest names but that is missing");

  CCA_CHECK(write_bytes(generation, original));
  expect_single_authoritative(root, 2, "after restoring the generation file");
}

// ---------------------------------------------------------------------------
// Staging files and uncommitted generations
// ---------------------------------------------------------------------------

CCA_TEST(corruption_stale_staging_files_are_deleted_on_open) {
  TempDir dir("corruption-staging");
  const std::filesystem::path root = dir.path() / "store";
  CCA_REQUIRE_OK(publish_generations(root, 2));
  expect_single_authoritative(root, 2, "before any sabotage");

  const std::filesystem::path staging =
      root / std::filesystem::path("staging-0000000000000000-1.tmp");
  const std::filesystem::path staging_rollback =
      root / std::filesystem::path("staging-deadbeefdeadbeef-rollback.tmp");
  const std::filesystem::path bystander =
      root / std::filesystem::path("not-staging-keep.tmp");
  CCA_CHECK(write_bytes(staging, std::vector<std::uint8_t>(128U, 0x5AU)));
  CCA_CHECK(write_bytes(staging_rollback, std::vector<std::uint8_t>(16U, 0xA5U)));
  CCA_CHECK(write_bytes(bystander, std::vector<std::uint8_t>(8U, 0x11U)));

  // Opening the store removes every staging file and adopts nothing.
  expect_single_authoritative(root, 2, "with a stale staging file present");
  CCA_CHECK(!std::filesystem::exists(staging));
  CCA_CHECK(!std::filesystem::exists(staging_rollback));
  // A file that is not a staging file is left alone.
  CCA_CHECK(std::filesystem::exists(bystander));
  CCA_CHECK_EQ(read_bytes(bystander).size(), static_cast<std::size_t>(8));

  // Read-only access does not clean anything up and does not change the store.
  CCA_CHECK(write_bytes(staging, std::vector<std::uint8_t>(4U, 0x01U)));
  StoreOptions read_only_options;
  read_only_options.root = root;
  CCA_ASSIGN(reader, AccountingStore::open_read_only(read_only_options));
  CCA_CHECK(reader.read_only());
  CCA_CHECK_EQ(reader.current_generation().value(), std::uint64_t{2});
  CCA_ASSIGN(latest, reader.latest());
  CCA_REQUIRE_OK(latest.verify());
  CCA_REQUIRE_OK(reader.close());
  CCA_CHECK(std::filesystem::exists(staging));
}

CCA_TEST(corruption_uncommitted_generation_file_is_never_adopted) {
  TempDir dir("corruption-orphan");
  const std::filesystem::path root = dir.path() / "store";
  CCA_REQUIRE_OK(publish_generations(root, 3));
  expect_single_authoritative(root, 3, "before any sabotage");

  // A complete, valid generation file that no manifest names.
  const std::filesystem::path current = current_generation_file(root);
  const std::vector<std::uint8_t> original = read_bytes(current);
  const std::filesystem::path orphan =
      root / std::filesystem::path(generation_name(9U));
  CCA_CHECK(write_bytes(orphan, original));

  expect_single_authoritative(root, 3, "with an uncommitted generation file");

  // Observing it through the public API: it is not part of the history.
  StoreOptions options;
  options.root = root;
  CCA_ASSIGN(store, AccountingStore::open(options));
  CCA_ASSIGN(history, store.history());
  CCA_CHECK_EQ(history.size(), static_cast<std::size_t>(3));
  CCA_CHECK_EQ(history[2].generation.value(), std::uint64_t{3});
  CCA_CHECK_CODE(store.load(AccountGeneration::of(9U).value()),
                 ErrorCode::GenerationNotRetained);
  // The manifest is the only authority: a generation file it does not name is
  // discarded when the store opens, and it was never adopted.
  CCA_CHECK(!std::filesystem::exists(orphan));
  CCA_CHECK_EQ(store.current_generation().value(), std::uint64_t{3});

  // The next real publication proceeds normally.
  CCA_ASSIGN(snapshot, facility_snapshot(7U, 3U));
  CCA_ASSIGN(attempt, AttemptId::from_material("corruption-orphan-next"));
  PublishRequest request;
  request.attempt = attempt;
  request.fingerprint = Digest::of_text("corruption-orphan-next");
  request.expected_previous = store.current_generation();
  request.expected_revision = snapshot.header().generations.revision;
  request.epoch = options.epoch;
  request.published_at = fixture_now();
  CCA_ASSIGN(outcome, store.publish(snapshot, request));
  CCA_CHECK_EQ(outcome.record.generation.value(), std::uint64_t{4});
  CCA_REQUIRE_OK(store.close());

  CCA_CHECK(!std::filesystem::exists(orphan));
  expect_single_authoritative(root, 4, "after the sweep");
}

CCA_TEST(corruption_uncommitted_file_at_the_next_generation_is_never_overwritten) {
  TempDir dir("corruption-collision");
  const std::filesystem::path root = dir.path() / "store";
  CCA_REQUIRE_OK(publish_generations(root, 3));

  StoreOptions options;
  options.root = root;
  CCA_ASSIGN(store, AccountingStore::open(options));
  CCA_CHECK_EQ(store.current_generation().value(), std::uint64_t{3});

  // A file that occupies the name of the generation that is about to be
  // published, placed after the store was opened. It is not a valid generation
  // and the manifest does not name it.
  const std::filesystem::path blocking =
      root / std::filesystem::path(generation_name(4U));
  const std::vector<std::uint8_t> blocker{'u', 'n', 'c', 'o', 'm', 'm', 'i', 't'};
  CCA_CHECK(write_bytes(blocking, blocker));

  CCA_ASSIGN(snapshot, facility_snapshot(9U, 3U));
  CCA_ASSIGN(attempt, AttemptId::from_material("corruption-collision"));
  PublishRequest request;
  request.attempt = attempt;
  request.fingerprint = Digest::of_text("corruption-collision");
  request.expected_previous = store.current_generation();
  request.expected_revision = snapshot.header().generations.revision;
  request.epoch = options.epoch;
  request.published_at = fixture_now();

  // The publication fails rather than overwriting a file it did not write, and
  // nothing about the committed generation changes.
  CCA_CHECK_CODE(store.publish(snapshot, request), ErrorCode::IoFailure);
  CCA_CHECK_EQ(store.current_generation().value(), std::uint64_t{3});
  CCA_CHECK_EQ(store.commit_sequence().value(), std::uint64_t{3});
  CCA_REQUIRE_OK(store.close());
  CCA_CHECK_EQ(read_bytes(blocking), blocker);

  // Neither the in-flight staging file nor a generation file the manifest does
  // not name was ever committed. Both are swept when the store is next opened,
  // so an interrupted publication heals instead of blocking the next one.
  const std::vector<std::filesystem::path> staging = files_with_prefix(root, "staging-");
  CCA_CHECK(!staging.empty());
  CCA_ASSIGN(reopened, AccountingStore::open(options));
  CCA_CHECK_EQ(reopened.current_generation().value(), std::uint64_t{3});
  CCA_CHECK_EQ(reopened.commit_sequence().value(), std::uint64_t{3});
  CCA_REQUIRE_OK(reopened.close());
  CCA_CHECK(files_with_prefix(root, "staging-").empty());
  CCA_CHECK(!std::filesystem::exists(blocking));
  expect_single_authoritative(root, 3, "after the sweep");

  // With the uncommitted file gone the next publication proceeds normally.
  CCA_ASSIGN(final_store, AccountingStore::open(options));
  CCA_ASSIGN(final_snapshot, facility_snapshot(9U, 3U));
  CCA_ASSIGN(final_attempt, AttemptId::from_material("corruption-collision-retry"));
  PublishRequest final_request;
  final_request.attempt = final_attempt;
  final_request.fingerprint = Digest::of_text("corruption-collision-retry");
  final_request.expected_previous = final_store.current_generation();
  final_request.expected_revision = final_snapshot.header().generations.revision;
  final_request.epoch = options.epoch;
  final_request.published_at = fixture_now();
  CCA_ASSIGN(final_outcome, final_store.publish(final_snapshot, final_request));
  CCA_CHECK_EQ(final_outcome.record.generation.value(), std::uint64_t{4});
  CCA_REQUIRE_OK(final_store.close());
  expect_single_authoritative(root, 4, "after the retry");
}

// ---------------------------------------------------------------------------
// Explicit rollback
// ---------------------------------------------------------------------------

CCA_TEST(corruption_adopt_previous_generation_rolls_back) {
  TempDir dir("corruption-adopt");
  const std::filesystem::path root = dir.path() / "store";
  CCA_REQUIRE_OK(publish_generations(root, 3));
  expect_single_authoritative(root, 3, "before the rollback");

  StoreOptions options;
  options.root = root;
  CCA_ASSIGN(store, AccountingStore::open(options));
  CCA_ASSIGN(history_before, store.history());
  CCA_CHECK_EQ(history_before.size(), static_cast<std::size_t>(3));
  CCA_CHECK_EQ(history_before[1].generation.value(), std::uint64_t{2});
  CCA_CHECK_EQ(store.commit_sequence().value(), std::uint64_t{3});
  const std::uint64_t rollback_commit = store.commit_sequence().value() + 1U;

  // The current generation becomes unusable while the store is open.
  const std::filesystem::path current = current_generation_file(root);
  const std::vector<std::uint8_t> original = read_bytes(current);
  CCA_CHECK(original.size() > 64U);
  CCA_CHECK(flip_byte(current, original.size() - 33U));
  CCA_CHECK_CODE(store.latest(), ErrorCode::IntegrityFailure);

  CCA_ASSIGN(report, store.adopt_previous_generation());
  CCA_CHECK_EQ(report.adopted.value(), std::uint64_t{2});
  CCA_CHECK_EQ(report.abandoned.value(), std::uint64_t{3});
  CCA_CHECK_EQ(report.adopted_digest.to_hex(), history_before[1].content_digest.to_hex());
  CCA_CHECK(!report.reason.empty());

  // The store moved backwards and recorded the rollback.
  CCA_CHECK_EQ(store.current_generation().value(), std::uint64_t{2});
  CCA_CHECK_EQ(store.commit_sequence().value(), rollback_commit);
  CCA_CHECK_CODE(store.load(AccountGeneration::of(3U).value()),
                 ErrorCode::GenerationNotRetained);

  CCA_ASSIGN(adopted, store.load(AccountGeneration::of(2U).value()));
  CCA_CHECK(adopted.header().recovered);
  CCA_CHECK_EQ(adopted.header().recovered_from_generation, std::uint64_t{3});
  CCA_CHECK_EQ(adopted.header().commit_sequence.value(), rollback_commit);
  CCA_REQUIRE_OK(adopted.verify());
  CCA_CHECK(adopted.ledger().verify_closure().exact);

  CCA_ASSIGN(history_after, store.history());
  CCA_CHECK_EQ(history_after.size(), static_cast<std::size_t>(2));
  CCA_CHECK_EQ(history_after[0].generation.value(), std::uint64_t{1});
  CCA_CHECK_EQ(history_after[1].generation.value(), std::uint64_t{2});
  CCA_CHECK_EQ(history_after[1].commit_sequence.value(), rollback_commit);
  CCA_CHECK(history_after[1].recovered);
  CCA_CHECK_EQ(history_after[1].recovered_from_generation, std::uint64_t{3});
  CCA_REQUIRE_OK(store.close());

  // The adoption survives a reopen, and the store resolves to exactly one
  // authoritative generation.
  expect_single_authoritative(root, 2, "after the rollback");
  CCA_ASSIGN(reopened, AccountingStore::open(options));
  CCA_CHECK_EQ(reopened.current_generation().value(), std::uint64_t{2});
  CCA_ASSIGN(recovered, reopened.load(AccountGeneration::of(2U).value()));
  CCA_CHECK(recovered.header().recovered);
  CCA_CHECK_EQ(recovered.header().recovered_from_generation, std::uint64_t{3});
  CCA_REQUIRE_OK(recovered.verify());
  CCA_ASSIGN(reopened_history, reopened.history());
  CCA_CHECK_EQ(reopened_history.size(), static_cast<std::size_t>(2));
  CCA_REQUIRE_OK(reopened.close());
}

CCA_TEST(corruption_adopt_without_a_candidate_is_store_corrupt) {
  TempDir dir("corruption-adopt-corrupt");
  const std::filesystem::path root = dir.path() / "store";
  CCA_REQUIRE_OK(publish_generations(root, 1));

  StoreOptions options;
  options.root = root;
  CCA_ASSIGN(store, AccountingStore::open(options));
  CCA_CHECK_EQ(store.current_generation().value(), std::uint64_t{1});
  const std::filesystem::path current = current_generation_file(root);
  const std::vector<std::uint8_t> original = read_bytes(current);
  CCA_CHECK(original.size() > 64U);
  CCA_CHECK(flip_byte(current, original.size() - 33U));

  // There is no older complete generation to adopt.
  CCA_CHECK_CODE(store.adopt_previous_generation(), ErrorCode::StoreCorrupt);
  // The failed rollback changed nothing.
  CCA_CHECK_EQ(store.current_generation().value(), std::uint64_t{1});
  CCA_CHECK_EQ(store.commit_sequence().value(), std::uint64_t{1});
  CCA_CHECK_EQ(read_bytes(current).size(), original.size());
  CCA_REQUIRE_OK(store.close());

  // The sabotaged generation is still refused on the next open.
  expect_open_refused(root, ErrorCode::IntegrityFailure,
                      "a single sabotaged generation");

  // A store that never published anything has nothing to adopt either.
  TempDir empty_dir("corruption-adopt-empty");
  StoreOptions empty_options;
  empty_options.root = empty_dir.path() / "store";
  CCA_ASSIGN(empty_store, AccountingStore::open(empty_options));
  CCA_CHECK(!empty_store.has_generation());
  CCA_CHECK_CODE(empty_store.adopt_previous_generation(), ErrorCode::StoreCorrupt);
  CCA_CHECK_EQ(empty_store.current_generation().value(),
               AccountGeneration::initial().value());
  CCA_REQUIRE_OK(empty_store.close());
}

CCA_TEST(corruption_a_store_with_no_manifest_discards_uncommitted_generations) {
  TempDir dir("corruption-no-manifest");
  const std::filesystem::path root = dir.path() / "store";

  // A store whose first publication was interrupted: a staging file and a
  // generation file exist, but no manifest was ever committed.
  StoreOptions options;
  options.root = root;
  CCA_ASSIGN(first, AccountingStore::open(options));
  CCA_REQUIRE_OK(first.close());
  const std::filesystem::path orphan = root / std::filesystem::path(generation_name(2U));
  const std::filesystem::path staging = root / std::filesystem::path("staging-orphan.tmp");
  CCA_CHECK(write_bytes(orphan, std::vector<std::uint8_t>(256U, 0x42U)));
  CCA_CHECK(write_bytes(staging, std::vector<std::uint8_t>(256U, 0x43U)));

  CCA_ASSIGN(store, AccountingStore::open(options));
  CCA_CHECK(!store.has_generation());
  CCA_CHECK_EQ(store.current_generation().value(), AccountGeneration::initial().value());
  CCA_CHECK_CODE(store.latest(), ErrorCode::NoPublishedGeneration);
  CCA_REQUIRE_OK(store.close());

  // Neither the staging file nor the uncommitted generation survived.
  CCA_CHECK(!std::filesystem::exists(orphan));
  CCA_CHECK(!std::filesystem::exists(staging));
  CCA_CHECK(files_with_prefix(root, "gen-").empty());

  // The store publishes normally afterwards.
  CCA_REQUIRE_OK(publish_generations(root, 1));
  expect_single_authoritative(root, 1, "after the interrupted first publication");
}
