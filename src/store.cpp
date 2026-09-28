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

#include "cooling_capacity_accounting/store.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "byte_io.hpp"
#include "cooling_capacity_accounting/version.hpp"
#include "file_lock.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace cooling_capacity_accounting {
namespace {

constexpr std::string_view kManifestMagic = "CCAMAN01";
constexpr std::string_view kGenerationMagic = "CCAGEN01";
constexpr std::string_view kManifestFileName = "MANIFEST";
constexpr std::string_view kManifestTemporaryName = "MANIFEST.tmp";
constexpr std::string_view kLockFileName = "LOCK";
constexpr std::size_t kMagicSize = 8;

using internal::ByteReader;
using internal::ByteWriter;
using internal::FileLock;

[[nodiscard]] std::string system_error_text(const std::error_code& code) {
  return code.message() + " (" +
         to_decimal(static_cast<std::uint64_t>(code.value())) + ")";
}

[[nodiscard]] Error io_error(std::string_view what, const std::filesystem::path& path,
                             const std::error_code& code) {
  return Error::of(ErrorCode::IoFailure, std::string(what))
      .with_subject(path.filename().string())
      .with_detail(system_error_text(code));
}

[[nodiscard]] std::string generation_file_name(std::uint64_t generation) {
  std::string digits = to_decimal(generation);
  while (digits.size() < 20U) {
    digits.insert(digits.begin(), '0');
  }
  return "gen-" + digits + ".cca";
}

[[nodiscard]] bool is_staging_name(std::string_view name) {
  return name.size() > 8U && name.rfind("staging-", 0) == 0;
}

/// True when two canonical store paths name the same directory. Windows file
/// systems are case insensitive, so the comparison folds case there.
[[nodiscard]] bool same_root(const std::filesystem::path& lhs,
                             const std::filesystem::path& rhs) {
  std::string left = lhs.generic_string();
  std::string right = rhs.generic_string();
#ifdef _WIN32
  left = to_lower_ascii(left);
  right = to_lower_ascii(right);
#endif
  return left == right;
}

/// Rejects a store root that could name two different directories, or a
/// directory reached through a reparse point. The canonical form is what the
/// lock file and every read and write use, so two processes cannot lock two
/// spellings of the same store.
[[nodiscard]] Result<std::filesystem::path> canonical_root(
    const std::filesystem::path& root) {
  if (root.empty()) {
    return Error::of(ErrorCode::PathInvalid, "the store root is empty");
  }
  const std::string text = root.generic_string();
  if (text.find('\0') != std::string::npos) {
    return Error::of(ErrorCode::PathInvalid, "the store root contains a NUL byte");
  }
  if (text.size() > kMaxPathLength) {
    return Error::of(ErrorCode::PathTooLong,
                     "the store root is longer than the bound allows")
        .with_detail("length " + to_decimal(static_cast<std::uint64_t>(text.size())) +
                     " exceeds " +
                     to_decimal(static_cast<std::uint64_t>(kMaxPathLength)));
  }
  for (const std::filesystem::path& part : root) {
    const std::string component = part.generic_string();
    if (component == "..") {
      return Error::of(ErrorCode::PathTraversalRejected,
                       "the store root contains a traversal component");
    }
  }
  std::error_code code;
  const std::filesystem::path absolute = std::filesystem::absolute(root, code);
  if (code) {
    return io_error("the store root could not be made absolute", root, code);
  }
  const std::filesystem::path normalized = absolute.lexically_normal();
  const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(normalized, code);
  if (code) {
    return io_error("the store root could not be canonicalised", root, code);
  }
  // Repaired spellings of one directory - a trailing separator, a "." component,
  // redundant separators - must all be accepted, because they name the same
  // store. Only a path that resolves somewhere else is a reparse point.
  const auto without_trailing_separator = [](std::string text) {
    while (text.size() > 1U && (text.back() == '/' || text.back() == '\\')) {
      text.pop_back();
    }
    return text;
  };
  // A path that changes when it is canonicalised traverses a symbolic link,
  // junction or other reparse point. Two spellings of one directory could
  // otherwise obtain two different locks, so the store refuses the path rather
  // than picking one of the spellings.
  const std::filesystem::path trimmed_canonical(
      without_trailing_separator(canonical.generic_string()));
  const std::filesystem::path trimmed_normalized(
      without_trailing_separator(normalized.generic_string()));
  const bool resolves_elsewhere = !same_root(trimmed_canonical, trimmed_normalized);
  std::error_code status_code;
  const bool present = std::filesystem::exists(canonical, status_code);
  if (status_code) {
    return io_error("the store root could not be inspected", canonical, status_code);
  }
  if (present) {
    std::error_code link_code;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(canonical, link_code);
    if (link_code) {
      return io_error("the store root could not be inspected", canonical, link_code);
    }
    if (std::filesystem::is_symlink(status)) {
      return Error::of(ErrorCode::ReparsePointRejected,
                       "the store root is a symbolic link or reparse point")
          .with_subject(canonical.string());
    }
  }
  if (resolves_elsewhere) {
    return Error::of(ErrorCode::ReparsePointRejected,
                     "the store root is reached through a symbolic link or reparse "
                     "point")
        .with_subject(root.string());
  }
  return trimmed_canonical;
}

[[nodiscard]] Result<void> flush_stream(std::FILE* file) {
  if (std::fflush(file) != 0) {
    return Error::of(ErrorCode::FlushFailed, "the buffered data could not be flushed");
  }
#ifdef _WIN32
  if (_commit(_fileno(file)) != 0) {
    return Error::of(ErrorCode::FlushFailed,
                     "the buffered data could not be committed to disk");
  }
#else
  if (::fsync(::fileno(file)) != 0) {
    return Error::of(ErrorCode::FlushFailed,
                     "the buffered data could not be committed to disk")
        .with_detail("errno " + to_decimal(static_cast<std::uint64_t>(errno)));
  }
#endif
  return Ok{};
}

[[nodiscard]] Result<void> write_file_durable(
    const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
#ifdef _WIN32
  std::FILE* file = nullptr;
  const errno_t opened =
      _wfopen_s(&file, path.wstring().c_str(), L"wb");
  if (opened != 0 || file == nullptr) {
    return Error::of(ErrorCode::IoFailure, "the file could not be created")
        .with_subject(path.filename().string());
  }
#else
  std::FILE* file = std::fopen(path.string().c_str(), "wb");
  if (file == nullptr) {
    return Error::of(ErrorCode::IoFailure, "the file could not be created")
        .with_subject(path.filename().string())
        .with_detail("errno " + to_decimal(static_cast<std::uint64_t>(errno)));
  }
#endif
  if (!bytes.empty() &&
      std::fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size()) {
    static_cast<void>(std::fclose(file));
    return Error::of(ErrorCode::IoFailure, "the file could not be written")
        .with_subject(path.filename().string());
  }
  const Result<void> flushed = flush_stream(file);
  static_cast<void>(std::fclose(file));
  CCA_TRY(flushed);
  return Ok{};
}

[[nodiscard]] Result<std::vector<std::uint8_t>> read_file(
    const std::filesystem::path& path, std::size_t max_bytes) {
  std::error_code size_code;
  const std::uintmax_t size = std::filesystem::file_size(path, size_code);
  if (size_code) {
    return io_error("the file size could not be read", path, size_code);
  }
  if (size > max_bytes) {
    return Error::of(ErrorCode::ItemTooLarge,
                     "the stored file is larger than the bound allows")
        .with_subject(path.filename().string())
        .with_detail("size " + to_decimal(static_cast<std::uint64_t>(size)) +
                     " exceeds " +
                     to_decimal(static_cast<std::uint64_t>(max_bytes)));
  }
#ifdef _WIN32
  std::FILE* file = nullptr;
  const errno_t opened = _wfopen_s(&file, path.wstring().c_str(), L"rb");
  if (opened != 0 || file == nullptr) {
    return Error::of(ErrorCode::IoFailure, "the file could not be opened")
        .with_subject(path.filename().string());
  }
#else
  std::FILE* file = std::fopen(path.string().c_str(), "rb");
  if (file == nullptr) {
    return Error::of(ErrorCode::IoFailure, "the file could not be opened")
        .with_subject(path.filename().string());
  }
#endif
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  if (!bytes.empty() &&
      std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) {
    static_cast<void>(std::fclose(file));
    return Error::of(ErrorCode::TruncatedInput, "the file ended before its length")
        .with_subject(path.filename().string());
  }
  static_cast<void>(std::fclose(file));
  return bytes;
}

/// Renames a file. On Windows the rename is write-through, and on POSIX the
/// containing directory is flushed afterwards, so a reader after a crash sees
/// either the old name or the new one and never a partial file.
[[nodiscard]] Result<void> rename_durable(const std::filesystem::path& source,
                                          const std::filesystem::path& target,
                                          bool replace) {
  std::error_code code;
  if (!replace) {
    std::error_code exists_code;
    const bool present = std::filesystem::exists(target, exists_code);
    if (exists_code) {
      return io_error("the destination could not be inspected", target, exists_code);
    }
    if (present) {
      return Error::of(ErrorCode::IoFailure,
                       "the destination already exists and is never overwritten "
                       "silently")
          .with_subject(target.filename().string());
    }
  }
#ifdef _WIN32
  DWORD flags = MOVEFILE_WRITE_THROUGH;
  if (replace) {
    flags |= MOVEFILE_REPLACE_EXISTING;
  }
  if (!MoveFileExW(source.wstring().c_str(), target.wstring().c_str(), flags)) {
    const unsigned long code_value = GetLastError();
    return Error::of(ErrorCode::IoFailure, "the file could not be renamed")
        .with_subject(target.filename().string())
        .with_detail("windows error " +
                     to_decimal(static_cast<std::uint64_t>(code_value)));
  }
  return Ok{};
#else
  std::filesystem::rename(source, target, code);
  if (code) {
    return io_error("the file could not be renamed", target, code);
  }
  const std::filesystem::path directory = target.parent_path().empty()
                                              ? std::filesystem::path(".")
                                              : target.parent_path();
  const int descriptor = ::open(directory.c_str(), O_RDONLY);
  if (descriptor >= 0) {
    static_cast<void>(::fsync(descriptor));
    static_cast<void>(::close(descriptor));
  }
  return Ok{};
#endif
}

[[nodiscard]] Result<void> remove_if_present(const std::filesystem::path& path) {
  std::error_code code;
  static_cast<void>(std::filesystem::remove(path, code));
  return Ok{};
}

// ---------------------------------------------------------------------------
// On-disk records
// ---------------------------------------------------------------------------

struct ManifestEntry {
  AccountGeneration generation;
  CommitSequence commit_sequence;
  std::uint64_t payload_bytes = 0;
  Digest content_digest;
};

struct AttemptEntry {
  AttemptId attempt;
  Digest fingerprint;
  AccountGeneration generation;
  CommitSequence commit_sequence;
  Timestamp published_at;
};

struct Manifest {
  IncarnationId incarnation;
  ControlPlaneEpoch epoch;
  CommitSequence commit_sequence;
  AccountGeneration current;
  std::vector<ManifestEntry> retained;
  std::vector<AttemptEntry> attempts;

  [[nodiscard]] bool has_current() const noexcept {
    for (const ManifestEntry& entry : retained) {
      if (entry.generation == current) {
        return true;
      }
    }
    return false;
  }
};

void write_digest(ByteWriter& writer, const Digest& digest) {
  writer.write_raw(digest.bytes().data(), digest.bytes().size());
}

[[nodiscard]] Result<Digest> read_digest(ByteReader& reader) {
  CCA_TRY_ASSIGN(bytes, reader.read_raw(Digest::kSize));
  std::array<std::uint8_t, Digest::kSize> value{};
  std::copy(bytes.begin(), bytes.end(), value.begin());
  return Digest::from_bytes(value);
}

[[nodiscard]] Result<std::vector<std::uint8_t>> encode_generation(
    const SnapshotHeader& header, const std::vector<std::uint8_t>& payload) {
  ByteWriter writer;
  writer.write_raw(kGenerationMagic.data(), kGenerationMagic.size());
  writer.write_u32(kStoreFormatVersion);
  writer.write_u32(0);
  writer.write_u64(header.generation.value());
  writer.write_u64(header.commit_sequence.value());
  writer.write_u64(header.generations.epoch.value());
  writer.write_i64(header.accounted_at.unix_milliseconds());
  writer.write_i64(header.published_at.unix_milliseconds());
  writer.write_u8(header.recovered ? 1U : 0U);
  for (int index = 0; index < 7; ++index) {
    writer.write_u8(0);
  }
  writer.write_u64(header.recovered_from_generation);
  writer.write_text(header.policy.view());
  writer.write_u32(static_cast<std::uint32_t>(header.policy_generation.value()));
  write_digest(writer, header.policy_digest);
  writer.write_u64(header.generations.epoch.value());
  writer.write_u64(header.generations.topology.value());
  writer.write_u64(header.generations.policy.value());
  writer.write_u64(header.generations.evidence.value());
  writer.write_u64(header.generations.revision.value());
  writer.write_u64(static_cast<std::uint64_t>(payload.size()));
  writer.write_raw(payload.data(), payload.size());
  const Digest digest = Digest::of_bytes(writer.bytes().data(), writer.bytes().size());
  write_digest(writer, digest);
  return writer.bytes();
}

/// Everything a stored generation file carries, plus its decoded payload.
struct GenerationFile {
  StoredGeneration record;
  std::vector<std::uint8_t> payload;
  std::uint64_t payload_bytes = 0;
};

[[nodiscard]] Result<GenerationFile> decode_generation(const std::vector<std::uint8_t>& file,
                                                       bool keep_payload) {
  ByteReader reader(file.data(), file.size());
  CCA_TRY_ASSIGN(magic, reader.read_raw(kMagicSize));
  if (std::string_view(reinterpret_cast<const char*>(magic.data()), magic.size()) !=
      kGenerationMagic) {
    return Error::of(ErrorCode::MalformedRecord,
                     "the generation file magic is wrong");
  }
  CCA_TRY_ASSIGN(version, reader.read_u32());
  if (version != kStoreFormatVersion) {
    return Error::of(ErrorCode::UnsupportedFormatVersion,
                     "the generation file format version is not supported")
        .with_detail("version " + to_decimal(static_cast<std::uint64_t>(version)));
  }
  CCA_TRY_ASSIGN(reserved, reader.read_u32());
  if (reserved != 0U) {
    return Error::of(ErrorCode::ReservedFieldNotZero,
                     "the generation file reserved field is not zero");
  }
  GenerationFile result;
  CCA_TRY_ASSIGN(generation, reader.read_u64());
  CCA_TRY_ASSIGN(generation_value, AccountGeneration::of(generation));
  result.record.generation = generation_value;
  CCA_TRY_ASSIGN(commit_sequence, reader.read_u64());
  CCA_TRY_ASSIGN(commit_sequence_value, CommitSequence::of(commit_sequence));
  result.record.commit_sequence = commit_sequence_value;
  CCA_TRY_ASSIGN(epoch, reader.read_u64());
  CCA_TRY_ASSIGN(epoch_value, ControlPlaneEpoch::of(epoch));
  result.record.epoch = epoch_value;
  CCA_TRY_ASSIGN(accounted_at, reader.read_i64());
  CCA_TRY_ASSIGN(accounted_at_value, Timestamp::checked_unix_milliseconds(accounted_at));
  result.record.accounted_at = accounted_at_value;
  CCA_TRY_ASSIGN(published_at, reader.read_i64());
  CCA_TRY_ASSIGN(published_at_value,
                 Timestamp::checked_unix_milliseconds(published_at));
  result.record.published_at = published_at_value;
  CCA_TRY_ASSIGN(recovered, reader.read_u8());
  if (recovered > 1U) {
    return Error::of(ErrorCode::MalformedRecord, "the recovered flag is neither 0 nor 1");
  }
  result.record.recovered = recovered == 1U;
  CCA_TRY_ASSIGN(reserved_bytes, reader.read_raw(7));
  for (const std::uint8_t byte : reserved_bytes) {
    if (byte != 0U) {
      return Error::of(ErrorCode::ReservedFieldNotZero,
                       "a generation file reserved byte is not zero");
    }
  }
  CCA_TRY_ASSIGN(recovered_from, reader.read_u64());
  result.record.recovered_from_generation = recovered_from;
  CCA_TRY_ASSIGN(policy_text, reader.read_text(kMaxIdentifierLength));
  CCA_TRY_ASSIGN(policy_id, PolicyId::parse(policy_text));
  result.record.policy = policy_id;
  CCA_TRY_ASSIGN(policy_generation, reader.read_u32());
  CCA_TRY_ASSIGN(policy_generation_value,
                 PolicyGeneration::of(policy_generation));
  result.record.policy_generation = policy_generation_value;
  CCA_TRY_ASSIGN(policy_digest, read_digest(reader));
  result.record.policy_digest = policy_digest;
  CCA_TRY_ASSIGN(bundle_epoch, reader.read_u64());
  CCA_TRY_ASSIGN(bundle_topology, reader.read_u64());
  CCA_TRY_ASSIGN(bundle_policy, reader.read_u64());
  CCA_TRY_ASSIGN(bundle_evidence, reader.read_u64());
  CCA_TRY_ASSIGN(bundle_revision, reader.read_u64());
  CCA_TRY_ASSIGN(epoch_bundle, ControlPlaneEpoch::of(bundle_epoch));
  CCA_TRY_ASSIGN(topology_bundle, TopologyGeneration::of(bundle_topology));
  CCA_TRY_ASSIGN(policy_bundle, PolicyGeneration::of(bundle_policy));
  CCA_TRY_ASSIGN(evidence_bundle, EvidenceGeneration::of(bundle_evidence));
  CCA_TRY_ASSIGN(revision_bundle, StateRevision::of(bundle_revision));
  result.record.generations.epoch = epoch_bundle;
  result.record.generations.topology = topology_bundle;
  result.record.generations.policy = policy_bundle;
  result.record.generations.evidence = evidence_bundle;
  result.record.generations.revision = revision_bundle;
  CCA_TRY_ASSIGN(payload_bytes, reader.read_u64());
  if (payload_bytes > kMaxCanonicalPayloadBytes) {
    return Error::of(ErrorCode::ItemTooLarge,
                     "the stored payload is larger than the bound allows")
        .with_detail(to_decimal(payload_bytes));
  }
  result.payload_bytes = payload_bytes;
  result.record.payload_bytes = payload_bytes;
  CCA_TRY_ASSIGN(payload, reader.read_raw(static_cast<std::size_t>(payload_bytes)));
  CCA_TRY_ASSIGN(file_digest, read_digest(reader));
  if (!reader.empty()) {
    return Error::of(ErrorCode::TrailingBytes,
                     "the generation file continues after its digest");
  }
  const std::size_t covered = file.size() - Digest::kSize;
  const Digest computed = Digest::of_bytes(file.data(), covered);
  if (computed != file_digest) {
    return Error::of(ErrorCode::IntegrityFailure,
                     "the generation file does not match its recorded digest")
        .with_detail("recorded " + file_digest.to_hex() + " computed " +
                     computed.to_hex());
  }
  const Digest payload_digest = Digest::of_bytes(payload.data(), payload.size());
  result.record.content_digest = payload_digest;
  if (keep_payload) {
    result.payload = std::move(payload);
  }
  return result;
}

[[nodiscard]] Result<std::vector<std::uint8_t>> encode_manifest(const Manifest& manifest) {
  ByteWriter writer;
  writer.write_raw(kManifestMagic.data(), kManifestMagic.size());
  writer.write_u32(kStoreFormatVersion);
  writer.write_u32(0);
  writer.write_u64(manifest.incarnation.high());
  writer.write_u64(manifest.incarnation.low());
  writer.write_u64(manifest.epoch.value());
  writer.write_u64(manifest.commit_sequence.value());
  writer.write_u64(manifest.current.value());
  writer.write_u32(static_cast<std::uint32_t>(manifest.retained.size()));
  for (const ManifestEntry& entry : manifest.retained) {
    writer.write_u64(entry.generation.value());
    writer.write_u64(entry.commit_sequence.value());
    writer.write_u64(entry.payload_bytes);
    write_digest(writer, entry.content_digest);
  }
  writer.write_u32(static_cast<std::uint32_t>(manifest.attempts.size()));
  for (const AttemptEntry& entry : manifest.attempts) {
    writer.write_u64(entry.attempt.high());
    writer.write_u64(entry.attempt.low());
    write_digest(writer, entry.fingerprint);
    writer.write_u64(entry.generation.value());
    writer.write_u64(entry.commit_sequence.value());
    writer.write_u64(static_cast<std::uint64_t>(entry.published_at.unix_milliseconds()));
  }
  const Digest digest = Digest::of_bytes(writer.bytes().data(), writer.bytes().size());
  write_digest(writer, digest);
  return writer.bytes();
}

/// Rebuilds a 128-bit identifier from its two 64-bit halves.
[[nodiscard]] WideId wide_id_from_halves(std::uint64_t high, std::uint64_t low) {
  std::array<std::uint8_t, Digest::kSize> bytes_value{};
  for (std::size_t index = 0; index < 8; ++index) {
    bytes_value[index] =
        static_cast<std::uint8_t>((high >> ((7U - index) * 8U)) & 0xFFU);
    bytes_value[index + 8] =
        static_cast<std::uint8_t>((low >> ((7U - index) * 8U)) & 0xFFU);
  }
  return WideId::from_digest(Digest::from_bytes(bytes_value));
}

[[nodiscard]] Result<Manifest> decode_manifest(const std::vector<std::uint8_t>& bytes,
                                               const Limits& limits) {
  ByteReader reader(bytes.data(), bytes.size());
  CCA_TRY_ASSIGN(magic, reader.read_raw(kMagicSize));
  if (std::string_view(reinterpret_cast<const char*>(magic.data()), magic.size()) !=
      kManifestMagic) {
    return Error::of(ErrorCode::MalformedRecord, "the manifest magic is wrong");
  }
  CCA_TRY_ASSIGN(version, reader.read_u32());
  if (version != kStoreFormatVersion) {
    return Error::of(ErrorCode::UnsupportedFormatVersion,
                     "the manifest format version is not supported")
        .with_detail("version " + to_decimal(static_cast<std::uint64_t>(version)));
  }
  CCA_TRY_ASSIGN(reserved, reader.read_u32());
  if (reserved != 0U) {
    return Error::of(ErrorCode::ReservedFieldNotZero,
                     "the manifest reserved field is not zero");
  }
  Manifest manifest;
  CCA_TRY_ASSIGN(incarnation_high, reader.read_u64());
  CCA_TRY_ASSIGN(incarnation_low, reader.read_u64());
  manifest.incarnation = wide_id_from_halves(incarnation_high, incarnation_low);
  CCA_TRY_ASSIGN(epoch, reader.read_u64());
  CCA_TRY_ASSIGN(epoch_value, ControlPlaneEpoch::of(epoch));
  manifest.epoch = epoch_value;
  CCA_TRY_ASSIGN(commit_sequence, reader.read_u64());
  CCA_TRY_ASSIGN(commit_sequence_value, CommitSequence::of(commit_sequence));
  manifest.commit_sequence = commit_sequence_value;
  CCA_TRY_ASSIGN(current, reader.read_u64());
  CCA_TRY_ASSIGN(current_value, AccountGeneration::of(current));
  manifest.current = current_value;
  CCA_TRY_ASSIGN(retained_count,
                 reader.read_count(limits.max_retained_generations, "retained generations"));
  for (std::uint32_t index = 0; index < retained_count; ++index) {
    ManifestEntry entry;
    CCA_TRY_ASSIGN(generation, reader.read_u64());
    CCA_TRY_ASSIGN(generation_value, AccountGeneration::of(generation));
    entry.generation = generation_value;
    CCA_TRY_ASSIGN(sequence, reader.read_u64());
    CCA_TRY_ASSIGN(sequence_value, CommitSequence::of(sequence));
    entry.commit_sequence = sequence_value;
    CCA_TRY_ASSIGN(payload_bytes, reader.read_u64());
    entry.payload_bytes = payload_bytes;
    CCA_TRY_ASSIGN(digest, read_digest(reader));
    entry.content_digest = digest;
    manifest.retained.push_back(entry);
  }
  CCA_TRY_ASSIGN(attempt_count,
                 reader.read_count(limits.max_attempt_records, "attempt records"));
  for (std::uint32_t index = 0; index < attempt_count; ++index) {
    AttemptEntry entry;
    CCA_TRY_ASSIGN(attempt_high, reader.read_u64());
    CCA_TRY_ASSIGN(attempt_low, reader.read_u64());
    entry.attempt = wide_id_from_halves(attempt_high, attempt_low);
    CCA_TRY_ASSIGN(fingerprint, read_digest(reader));
    entry.fingerprint = fingerprint;
    CCA_TRY_ASSIGN(generation, reader.read_u64());
    CCA_TRY_ASSIGN(generation_value, AccountGeneration::of(generation));
    entry.generation = generation_value;
    CCA_TRY_ASSIGN(sequence, reader.read_u64());
    CCA_TRY_ASSIGN(sequence_value, CommitSequence::of(sequence));
    entry.commit_sequence = sequence_value;
    CCA_TRY_ASSIGN(published_at, reader.read_u64());
    CCA_TRY_ASSIGN(published_at_value,
                   Timestamp::checked_unix_milliseconds(
                       static_cast<std::int64_t>(published_at)));
    entry.published_at = published_at_value;
    manifest.attempts.push_back(entry);
  }
  CCA_TRY_ASSIGN(digest, read_digest(reader));
  if (!reader.empty()) {
    return Error::of(ErrorCode::TrailingBytes,
                     "the manifest continues after its digest");
  }
  const std::size_t covered = bytes.size() - Digest::kSize;
  const Digest computed = Digest::of_bytes(bytes.data(), covered);
  if (computed != digest) {
    return Error::of(ErrorCode::IntegrityFailure,
                     "the manifest does not match its recorded digest")
        .with_detail("recorded " + digest.to_hex() + " computed " + computed.to_hex());
  }
  return manifest;
}

}  // namespace

struct AccountingStore::Impl {
  StoreOptions options;
  std::filesystem::path root;
  FileLock lock;
  Manifest manifest;
  bool open = false;
  bool read_only = false;
  bool has_generation = false;
};

AccountingStore::AccountingStore() = default;
AccountingStore::~AccountingStore() {
  if (impl_ != nullptr && impl_->open) {
    const Result<void> closed = close();
    static_cast<void>(closed);
  }
}

AccountingStore::AccountingStore(AccountingStore&& other) noexcept = default;
AccountingStore& AccountingStore::operator=(AccountingStore&& other) noexcept = default;

Result<void> AccountingStore::validate_root(const std::filesystem::path& root) {
  CCA_TRY_ASSIGN(canonical, canonical_root(root));
  static_cast<void>(canonical);
  return Ok{};
}

Result<AccountingStore> AccountingStore::open(const StoreOptions& options) {
  CCA_TRY(options.limits.validate());
  CCA_TRY_ASSIGN(root, canonical_root(options.root));

  std::error_code code;
  const bool exists = std::filesystem::exists(root, code);
  if (code) {
    return io_error("the store root could not be inspected", root, code);
  }
  if (!exists) {
    if (!options.create_if_missing) {
      return Error::of(ErrorCode::StoreNotFound, "the store directory does not exist")
          .with_subject(root.string());
    }
    std::error_code create_code;
    static_cast<void>(std::filesystem::create_directories(root, create_code));
    if (create_code) {
      if (create_code.value() == static_cast<int>(std::errc::permission_denied)) {
        return Error::of(ErrorCode::PermissionDenied,
                         "the store directory could not be created")
            .with_subject(root.string());
      }
      return io_error("the store directory could not be created", root, create_code);
    }
  } else if (!std::filesystem::is_directory(root, code)) {
    return Error::of(ErrorCode::PathInvalid, "the store root is not a directory")
        .with_subject(root.string());
  }

  CCA_TRY_ASSIGN(lock,
                 FileLock::acquire(root / std::filesystem::path(kLockFileName), true,
                                   true));

  AccountingStore store;
  store.impl_ = std::make_unique<Impl>();
  store.impl_->options = options;
  store.impl_->root = root;
  store.impl_->lock = std::move(lock);
  store.impl_->open = true;
  store.impl_->read_only = false;

  // Staging files are never authoritative: a crash before the commit point
  // leaves them, and they are removed here.
  std::vector<std::filesystem::path> staging;
  std::error_code iterate_code;
  std::filesystem::directory_iterator iterator(root, iterate_code);
  if (iterate_code) {
    return io_error("the store directory could not be listed", root, iterate_code);
  }
  for (const std::filesystem::directory_entry& entry : iterator) {
    if (is_staging_name(entry.path().filename().string())) {
      staging.push_back(entry.path());
    }
  }
  std::sort(staging.begin(), staging.end());
  for (const std::filesystem::path& path : staging) {
    CCA_TRY(remove_if_present(path));
  }

  const std::filesystem::path manifest_path = root / std::filesystem::path(kManifestFileName);
  std::error_code manifest_code;
  const bool manifest_present = std::filesystem::exists(manifest_path, manifest_code);
  if (manifest_code) {
    return io_error("the manifest could not be inspected", manifest_path, manifest_code);
  }
  if (!manifest_present) {
    store.impl_->manifest = Manifest{};
    store.impl_->manifest.incarnation = store.impl_->options.incarnation;
    store.impl_->manifest.epoch = options.epoch;
    store.impl_->manifest.current = AccountGeneration::initial();
    store.impl_->has_generation = false;
    // An uncommitted generation file is exactly what an interrupted first
    // publication leaves behind; it is discarded because it was never named by a
    // manifest, and the manifest is the only authority.
    std::error_code orphan_code;
    std::filesystem::directory_iterator orphans(root, orphan_code);
    if (!orphan_code) {
      std::vector<std::filesystem::path> orphaned;
      for (const std::filesystem::directory_entry& entry : orphans) {
        const std::string name = entry.path().filename().string();
        if (name.size() > 4U && name.rfind("gen-", 0) == 0) {
          orphaned.push_back(entry.path());
        }
      }
      std::sort(orphaned.begin(), orphaned.end());
      for (const std::filesystem::path& path : orphaned) {
        CCA_TRY(remove_if_present(path));
      }
    }
    return store;
  }

  CCA_TRY_ASSIGN(bytes, read_file(manifest_path, kMaxCanonicalRecordBytes));
  CCA_TRY_ASSIGN(manifest, decode_manifest(bytes, options.limits));
  if (manifest.epoch != options.epoch) {
    if (manifest.epoch > options.epoch) {
      return Error::of(ErrorCode::StaleEpoch,
                       "the store was written under a later control-plane epoch")
          .with_detail("store epoch " + to_decimal(manifest.epoch.value()) +
                       " writer epoch " + to_decimal(options.epoch.value()));
    }
    return Error::of(ErrorCode::CrossEpochAuthority,
                     "the store was written under a different control-plane epoch")
        .with_detail("store epoch " + to_decimal(manifest.epoch.value()) +
                     " writer epoch " + to_decimal(options.epoch.value()));
  }
  store.impl_->manifest = manifest;
  store.impl_->has_generation = true;

  // The authoritative generation must be present and intact before the store
  // reports itself open.
  // A publication that was interrupted after the generation rename but before
  // the manifest rename leaves a generation file nothing names. It was never
  // committed, so it is removed rather than adopted, and its number is free
  // again for the next publication.
  std::vector<std::filesystem::path> orphans;
  std::error_code list_code;
  std::filesystem::directory_iterator entries(root, list_code);
  if (list_code) {
    return io_error("the store directory could not be listed", root, list_code);
  }
  for (const std::filesystem::directory_entry& entry : entries) {
    const std::string name = entry.path().filename().string();
    if (name.size() <= 4U || name.rfind("gen-", 0) != 0) {
      continue;
    }
    bool named = false;
    for (const ManifestEntry& retained : manifest.retained) {
      if (generation_file_name(retained.generation.value()) == name) {
        named = true;
        break;
      }
    }
    if (!named) {
      orphans.push_back(entry.path());
    }
  }
  std::sort(orphans.begin(), orphans.end());
  for (const std::filesystem::path& path : orphans) {
    CCA_TRY(remove_if_present(path));
  }

  const std::filesystem::path generation_path =
      root / std::filesystem::path(generation_file_name(manifest.current.value()));
  std::error_code exists_code;
  if (!std::filesystem::exists(generation_path, exists_code)) {
    return Error::of(ErrorCode::IntegrityFailure,
                     "the manifest names a generation whose file is missing")
        .with_subject(generation_path.filename().string());
  }
  CCA_TRY_ASSIGN(file, read_file(generation_path,
                                 kMaxCanonicalPayloadBytes + kMaxCanonicalRecordBytes));
  CCA_TRY_ASSIGN(decoded, decode_generation(file, false));
  if (decoded.record.generation != manifest.current) {
    return Error::of(ErrorCode::IntegrityFailure,
                     "the generation file holds a different generation than the "
                     "manifest names");
  }
  return store;
}

Result<AccountingStore> AccountingStore::open_read_only(const StoreOptions& options) {
  CCA_TRY(options.limits.validate());
  CCA_TRY_ASSIGN(root, canonical_root(options.root));
  std::error_code code;
  if (!std::filesystem::exists(root, code)) {
    return Error::of(ErrorCode::StoreNotFound, "the store directory does not exist")
        .with_subject(root.string());
  }
  CCA_TRY_ASSIGN(lock,
                 FileLock::acquire(root / std::filesystem::path(kLockFileName), false,
                                   false));
  AccountingStore store;
  store.impl_ = std::make_unique<Impl>();
  store.impl_->options = options;
  store.impl_->options.create_if_missing = false;
  store.impl_->root = root;
  store.impl_->lock = std::move(lock);
  store.impl_->open = true;
  store.impl_->read_only = true;

  const std::filesystem::path manifest_path = root / std::filesystem::path(kManifestFileName);
  std::error_code manifest_code;
  if (!std::filesystem::exists(manifest_path, manifest_code)) {
    store.impl_->manifest = Manifest{};
    store.impl_->manifest.epoch = options.epoch;
    store.impl_->manifest.current = AccountGeneration::initial();
    store.impl_->has_generation = false;
    return store;
  }
  CCA_TRY_ASSIGN(bytes, read_file(manifest_path, kMaxCanonicalRecordBytes));
  CCA_TRY_ASSIGN(manifest, decode_manifest(bytes, options.limits));
  store.impl_->manifest = manifest;
  store.impl_->has_generation = true;
  const std::filesystem::path generation_path =
      root / std::filesystem::path(generation_file_name(manifest.current.value()));
  CCA_TRY_ASSIGN(file, read_file(generation_path,
                                 kMaxCanonicalPayloadBytes + kMaxCanonicalRecordBytes));
  CCA_TRY(decode_generation(file, false));
  return store;
}

bool AccountingStore::is_open() const noexcept {
  return impl_ != nullptr && impl_->open;
}

bool AccountingStore::read_only() const noexcept {
  return impl_ != nullptr && impl_->read_only;
}

const StoreOptions& AccountingStore::options() const noexcept {
  static const StoreOptions kEmpty{};
  return impl_ != nullptr ? impl_->options : kEmpty;
}

WriterFence AccountingStore::fence() const noexcept {
  WriterFence fence;
  if (impl_ == nullptr) {
    return fence;
  }
  fence.incarnation = impl_->manifest.incarnation;
  fence.epoch = impl_->manifest.epoch;
  fence.last_commit = impl_->manifest.commit_sequence;
  return fence;
}

AccountGeneration AccountingStore::current_generation() const noexcept {
  if (impl_ == nullptr) {
    return AccountGeneration::initial();
  }
  return impl_->manifest.current;
}

bool AccountingStore::has_generation() const noexcept {
  return impl_ != nullptr && impl_->has_generation;
}

CommitSequence AccountingStore::commit_sequence() const noexcept {
  if (impl_ == nullptr) {
    return CommitSequence::initial();
  }
  return impl_->manifest.commit_sequence;
}

Result<PublishOutcome> AccountingStore::publish(const AccountingSnapshot& snapshot,
                                                const PublishRequest& request) {
  if (impl_ == nullptr || !impl_->open) {
    return Error::of(ErrorCode::UnexpectedState, "the store is not open");
  }
  if (impl_->read_only) {
    return Error::of(ErrorCode::NotSupported,
                     "the store was opened read only and publishes nothing");
  }
  Manifest& manifest = impl_->manifest;
  if (request.epoch != manifest.epoch) {
    if (request.epoch < manifest.epoch) {
      return Error::of(ErrorCode::StaleEpoch,
                       "the writer holds an older control-plane epoch than the store")
          .with_detail("writer epoch " + to_decimal(request.epoch.value()) +
                       " store epoch " + to_decimal(manifest.epoch.value()));
    }
    return Error::of(ErrorCode::CrossEpochAuthority,
                     "the writer holds a different control-plane epoch than the store")
        .with_detail("writer epoch " + to_decimal(request.epoch.value()) +
                     " store epoch " + to_decimal(manifest.epoch.value()));
  }
  if (request.expected_previous != manifest.current) {
    return Error::of(ErrorCode::PublishPreconditionFailed,
                     "the publish was planned against a generation that is no longer "
                     "current")
        .with_detail("expected " + to_decimal(request.expected_previous.value()) +
                     " current " + to_decimal(manifest.current.value()));
  }

  // Idempotent retry: the same attempt with the same fingerprint replays the
  // recorded outcome without actuating anything again.
  for (const AttemptEntry& entry : manifest.attempts) {
    if (entry.attempt != request.attempt) {
      continue;
    }
    if (entry.fingerprint != request.fingerprint) {
      return Error::of(ErrorCode::IdempotencyConflict,
                       "the attempt identifier was reused for a different request")
          .with_detail("attempt " + request.attempt.to_string());
    }
    CCA_TRY_ASSIGN(recorded, load(entry.generation));
    PublishOutcome outcome;
    outcome.replayed = true;
    outcome.record.generation = entry.generation;
    outcome.record.commit_sequence = entry.commit_sequence;
    outcome.record.content_digest = recorded.content_digest();
    outcome.record.published_at = entry.published_at;
    outcome.record.epoch = manifest.epoch;
    outcome.record.accounted_at = recorded.header().accounted_at;
    outcome.record.generations = recorded.header().generations;
    outcome.record.policy = recorded.header().policy;
    outcome.record.policy_generation = recorded.header().policy_generation;
    outcome.record.policy_digest = recorded.header().policy_digest;
    outcome.record.payload_bytes =
        static_cast<std::uint64_t>(recorded.canonical_bytes().size());
    outcome.record.recovered = recorded.header().recovered;
    outcome.record.recovered_from_generation =
        recorded.header().recovered_from_generation;
    return outcome;
  }

  // The first publication into an empty store takes the initial generation
  // itself; every later one advances by one.
  AccountGeneration next = manifest.current;
  if (impl_->has_generation) {
    CCA_TRY_ASSIGN(advanced, manifest.current.next());
    next = advanced;
  }
  CCA_TRY_ASSIGN(next_commit, manifest.commit_sequence.next());

  SnapshotHeader header = snapshot.header();
  header.generation = next;
  // The generation records the commit sequence this publication will take, not
  // the one it replaces, so the file and the manifest agree after the commit.
  header.commit_sequence = next_commit;
  header.published_at = request.published_at;
  header.recovered = false;
  header.recovered_from_generation = 0;
  header.writer = manifest.incarnation;

  CCA_TRY_ASSIGN(payload, encode_generation(header, snapshot.canonical_bytes()));
  const std::filesystem::path staging_path =
      impl_->root / std::filesystem::path(
                        "staging-" + manifest.incarnation.to_string() + "-" +
                        to_decimal(next.value()) + ".tmp");
  CCA_TRY(remove_if_present(staging_path));
  CCA_TRY(write_file_durable(staging_path, payload));
  // Read back exactly what was written before it becomes authoritative.
  CCA_TRY_ASSIGN(readback, read_file(staging_path, kMaxCanonicalPayloadBytes +
                                                       kMaxCanonicalRecordBytes));
  if (readback != payload) {
    CCA_TRY(remove_if_present(staging_path));
    return Error::of(ErrorCode::FlushFailed,
                     "the staged generation did not read back byte for byte");
  }
  CCA_TRY_ASSIGN(decoded, decode_generation(readback, false));
  if (decoded.record.generation != next) {
    CCA_TRY(remove_if_present(staging_path));
    return Error::of(ErrorCode::InternalError,
                     "the staged generation decoded to a different generation");
  }

  const std::filesystem::path generation_path =
      impl_->root / std::filesystem::path(generation_file_name(next.value()));
  CCA_TRY(rename_durable(staging_path, generation_path, false));

  Manifest updated;
  updated.incarnation = manifest.incarnation;
  updated.epoch = manifest.epoch;
  updated.commit_sequence = next_commit;
  updated.current = next;
  updated.retained = manifest.retained;
  updated.retained.push_back(
      ManifestEntry{next, updated.commit_sequence,
                    static_cast<std::uint64_t>(snapshot.canonical_bytes().size()),
                    snapshot.content_digest()});
  std::sort(updated.retained.begin(), updated.retained.end(),
            [](const ManifestEntry& lhs, const ManifestEntry& rhs) {
              return lhs.generation < rhs.generation;
            });
  const std::size_t retention =
      std::min<std::size_t>(impl_->options.retention_generations,
                            impl_->options.limits.max_retained_generations);
  while (updated.retained.size() > retention) {
    updated.retained.erase(updated.retained.begin());
  }
  updated.attempts = manifest.attempts;
  updated.attempts.push_back(AttemptEntry{request.attempt, request.fingerprint, next,
                                          updated.commit_sequence,
                                          request.published_at});
  const std::size_t attempt_limit =
      std::min<std::size_t>(impl_->options.limits.max_attempt_records,
                            kMaxAttemptRecords);
  while (updated.attempts.size() > attempt_limit) {
    updated.attempts.erase(updated.attempts.begin());
  }

  CCA_TRY_ASSIGN(manifest_bytes, encode_manifest(updated));
  const std::filesystem::path temporary_manifest =
      impl_->root / std::filesystem::path(kManifestTemporaryName);
  CCA_TRY(remove_if_present(temporary_manifest));
  CCA_TRY(write_file_durable(temporary_manifest, manifest_bytes));
  CCA_TRY_ASSIGN(manifest_readback,
                 read_file(temporary_manifest, kMaxCanonicalRecordBytes));
  if (manifest_readback != manifest_bytes) {
    CCA_TRY(remove_if_present(temporary_manifest));
    return Error::of(ErrorCode::FlushFailed,
                     "the staged manifest did not read back byte for byte");
  }
  CCA_TRY(decode_manifest(manifest_readback, impl_->options.limits));

  // The rename below is the commit point of the whole publication.
  const std::filesystem::path manifest_path =
      impl_->root / std::filesystem::path(kManifestFileName);
  CCA_TRY(rename_durable(temporary_manifest, manifest_path, true));

  impl_->manifest = updated;
  impl_->has_generation = true;

  // Retention: generations the manifest no longer names are removed. A failure
  // here is not a failure to commit.
  std::error_code iterate_code;
  std::filesystem::directory_iterator iterator(impl_->root, iterate_code);
  if (!iterate_code) {
    std::vector<std::filesystem::path> candidates;
    for (const std::filesystem::directory_entry& entry : iterator) {
      const std::string name = entry.path().filename().string();
      if (name.size() > 4U && name.rfind("gen-", 0) == 0) {
        candidates.push_back(entry.path());
      }
    }
    std::sort(candidates.begin(), candidates.end());
    for (const std::filesystem::path& path : candidates) {
      bool retained = false;
      for (const ManifestEntry& entry : impl_->manifest.retained) {
        if (generation_file_name(entry.generation.value()) ==
            path.filename().string()) {
          retained = true;
          break;
        }
      }
      if (!retained) {
        static_cast<void>(remove_if_present(path));
      }
    }
  }

  PublishOutcome outcome;
  outcome.replayed = false;
  outcome.record.generation = next;
  outcome.record.commit_sequence = updated.commit_sequence;
  outcome.record.epoch = updated.epoch;
  outcome.record.generations = header.generations;
  outcome.record.policy = header.policy;
  outcome.record.policy_generation = header.policy_generation;
  outcome.record.policy_digest = header.policy_digest;
  outcome.record.content_digest = snapshot.content_digest();
  outcome.record.accounted_at = header.accounted_at;
  outcome.record.published_at = header.published_at;
  outcome.record.payload_bytes =
      static_cast<std::uint64_t>(snapshot.canonical_bytes().size());
  return outcome;
}

Result<AccountingSnapshot> AccountingStore::latest() const {
  if (impl_ == nullptr || !impl_->open) {
    return Error::of(ErrorCode::UnexpectedState, "the store is not open");
  }
  if (!impl_->has_generation) {
    return Error::of(ErrorCode::NoPublishedGeneration,
                     "the store holds no published generation");
  }
  return load(impl_->manifest.current);
}

Result<AccountingSnapshot> AccountingStore::load(AccountGeneration generation) const {
  if (impl_ == nullptr || !impl_->open) {
    return Error::of(ErrorCode::UnexpectedState, "the store is not open");
  }
  const ManifestEntry* entry = nullptr;
  for (const ManifestEntry& candidate : impl_->manifest.retained) {
    if (candidate.generation == generation) {
      entry = &candidate;
      break;
    }
  }
  if (entry == nullptr) {
    return Error::of(ErrorCode::GenerationNotRetained,
                     "the generation is not retained by this store")
        .with_detail(to_decimal(generation.value()));
  }
  const std::filesystem::path path =
      impl_->root / std::filesystem::path(generation_file_name(generation.value()));
  CCA_TRY_ASSIGN(bytes,
                 read_file(path, kMaxCanonicalPayloadBytes + kMaxCanonicalRecordBytes));
  CCA_TRY_ASSIGN(decoded, decode_generation(bytes, true));
  if (decoded.record.generation != generation) {
    return Error::of(ErrorCode::IntegrityFailure,
                     "the generation file holds a different generation than expected");
  }
  if (decoded.record.content_digest != entry->content_digest) {
    return Error::of(ErrorCode::DigestMismatch,
                     "the stored payload digest is not the digest the manifest "
                     "recorded")
        .with_detail("manifest " + entry->content_digest.to_hex() + " file " +
                     decoded.record.content_digest.to_hex());
  }
  SnapshotHeader header;
  header.generation = decoded.record.generation;
  header.commit_sequence = decoded.record.commit_sequence;
  header.generations = decoded.record.generations;
  header.policy = decoded.record.policy;
  header.policy_generation = decoded.record.policy_generation;
  header.policy_digest = decoded.record.policy_digest;
  header.content_digest = decoded.record.content_digest;
  header.accounted_at = decoded.record.accounted_at;
  header.published_at = decoded.record.published_at;
  header.writer = impl_->manifest.incarnation;
  header.recovered = decoded.record.recovered;
  header.recovered_from_generation = decoded.record.recovered_from_generation;
  return AccountingSnapshot::from_canonical(decoded.payload, header,
                                            impl_->options.limits);
}

Result<std::vector<StoredGeneration>> AccountingStore::history() const {
  if (impl_ == nullptr || !impl_->open) {
    return Error::of(ErrorCode::UnexpectedState, "the store is not open");
  }
  std::vector<StoredGeneration> records;
  for (const ManifestEntry& entry : impl_->manifest.retained) {
    const std::filesystem::path path =
        impl_->root / std::filesystem::path(generation_file_name(entry.generation.value()));
    CCA_TRY_ASSIGN(bytes, read_file(path, kMaxCanonicalPayloadBytes +
                                                kMaxCanonicalRecordBytes));
    CCA_TRY_ASSIGN(decoded, decode_generation(bytes, false));
    if (decoded.record.content_digest != entry.content_digest) {
      return Error::of(ErrorCode::DigestMismatch,
                       "a retained generation does not match the digest the manifest "
                       "recorded")
          .with_subject(path.filename().string());
    }
    records.push_back(decoded.record);
  }
  std::sort(records.begin(), records.end(),
            [](const StoredGeneration& lhs, const StoredGeneration& rhs) {
              return lhs.generation < rhs.generation;
            });
  return records;
}

Result<RecoveryReport> AccountingStore::adopt_previous_generation() {
  if (impl_ == nullptr || !impl_->open) {
    return Error::of(ErrorCode::UnexpectedState, "the store is not open");
  }
  if (impl_->read_only) {
    return Error::of(ErrorCode::NotSupported,
                     "the store was opened read only and cannot roll back");
  }
  const AccountGeneration abandoned = impl_->manifest.current;
  std::vector<ManifestEntry> candidates = impl_->manifest.retained;
  std::sort(candidates.begin(), candidates.end(),
            [](const ManifestEntry& lhs, const ManifestEntry& rhs) {
              return rhs.generation < lhs.generation;
            });
  for (const ManifestEntry& candidate : candidates) {
    if (candidate.generation >= abandoned) {
      continue;
    }
    const std::filesystem::path path =
        impl_->root /
        std::filesystem::path(generation_file_name(candidate.generation.value()));
    std::error_code exists_code;
    if (!std::filesystem::exists(path, exists_code)) {
      continue;
    }
    const Result<std::vector<std::uint8_t>> bytes =
        read_file(path, kMaxCanonicalPayloadBytes + kMaxCanonicalRecordBytes);
    if (!bytes.ok()) {
      continue;
    }
    const Result<GenerationFile> decoded = decode_generation(bytes.value(), true);
    if (!decoded.ok()) {
      continue;
    }
    if (decoded.value().record.content_digest != candidate.content_digest) {
      continue;
    }
    SnapshotHeader header;
    header.generation = decoded.value().record.generation;
    CCA_TRY_ASSIGN(rollback_commit, impl_->manifest.commit_sequence.next());
    header.commit_sequence = rollback_commit;
    header.generations = decoded.value().record.generations;
    header.policy = decoded.value().record.policy;
    header.policy_generation = decoded.value().record.policy_generation;
    header.policy_digest = decoded.value().record.policy_digest;
    header.accounted_at = decoded.value().record.accounted_at;
    header.published_at = decoded.value().record.published_at;
    header.writer = impl_->manifest.incarnation;
    header.recovered = true;
    header.recovered_from_generation = abandoned.value();
    CCA_TRY_ASSIGN(payload, encode_generation(header, decoded.value().payload));
    const std::filesystem::path staging =
        impl_->root / std::filesystem::path("staging-" +
                                            impl_->manifest.incarnation.to_string() +
                                            "-rollback.tmp");
    CCA_TRY(remove_if_present(staging));
    CCA_TRY(write_file_durable(staging, payload));
    CCA_TRY(rename_durable(staging, path, true));

    Manifest updated = impl_->manifest;
    updated.current = candidate.generation;
    updated.commit_sequence = header.commit_sequence;
    for (ManifestEntry& entry : updated.retained) {
      if (entry.generation == candidate.generation) {
        entry.commit_sequence = header.commit_sequence;
      }
    }
    updated.retained.erase(
        std::remove_if(updated.retained.begin(), updated.retained.end(),
                       [&abandoned](const ManifestEntry& entry) {
                         return entry.generation == abandoned;
                       }),
        updated.retained.end());
    CCA_TRY_ASSIGN(manifest_bytes, encode_manifest(updated));
    const std::filesystem::path temporary_manifest =
        impl_->root / std::filesystem::path(kManifestTemporaryName);
    CCA_TRY(remove_if_present(temporary_manifest));
    CCA_TRY(write_file_durable(temporary_manifest, manifest_bytes));
    const std::filesystem::path manifest_path =
        impl_->root / std::filesystem::path(kManifestFileName);
    CCA_TRY(rename_durable(temporary_manifest, manifest_path, true));
    impl_->manifest = updated;

    RecoveryReport report;
    report.adopted = candidate.generation;
    report.abandoned = abandoned;
    report.adopted_digest = candidate.content_digest;
    report.reason =
        "the authoritative generation did not verify; the highest older complete "
        "generation was adopted explicitly and the rollback was recorded in the "
        "manifest and in the adopted generation's header";
    return report;
  }
  return Error::of(ErrorCode::StoreCorrupt,
                   "no older complete generation could be adopted");
}

Result<void> AccountingStore::close() {
  if (impl_ == nullptr) {
    return Ok{};
  }
  CCA_TRY(impl_->lock.release());
  impl_->open = false;
  return Ok{};
}

}  // namespace cooling_capacity_accounting
