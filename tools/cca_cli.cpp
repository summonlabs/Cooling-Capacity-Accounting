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

// cca - inspection and administration for Cooling Capacity Accounting.
//
// The tool never invents data: every scenario it can build is labelled
// SYNTHETIC in its own output, and every other command reads a store this
// library published.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

namespace {

using namespace cooling_capacity_accounting;

constexpr int kExitOk = 0;
constexpr int kExitUsage = 1;
constexpr int kExitFailed = 2;
constexpr int kExitVerificationFailed = 3;

void print_usage() {
  std::cout << "cca " << version_string() << " - " << kProductName << "\n\n"
            << "Usage:\n"
            << "  cca --version\n"
            << "  cca <store-dir> --self-check\n"
            << "  cca <store-dir> --scenario [--seed N] [--units N]\n"
            << "  cca <store-dir> --report [--scope ID] [--class ID] [--medium Air|Liquid]\n"
            << "  cca <store-dir> --verify\n"
            << "  cca <store-dir> --history\n"
            << "  cca <store-dir> --export-canonical <file>\n"
            << "  cca <store-dir> --export-interchange <file>\n"
            << "  cca --check <interchange-file>\n"
            << "  cca <store-dir> --ingest <interchange-file> [--accounted-at TS]\n\n"
            << "Every scenario this tool builds is SYNTHETIC facility data; nothing it\n"
            << "prints was measured on real cooling equipment.\n\n"
            << "Exit codes: 0 success, 1 usage, 2 operation failed, 3 verification failed.\n";
}

struct Options {
  std::string store;
  std::string command;
  std::string file;
  std::string scope;
  std::string klass;
  std::string medium;
  std::string accounted_at;
  std::uint64_t seed = 20260101ULL;
  std::int64_t units = 3;
  bool has_units = false;
};

[[nodiscard]] bool parse_options(int argc, char** argv, Options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--version") {
      options.command = "version";
      continue;
    }
    if (argument == "--help" || argument == "-h") {
      options.command = "help";
      continue;
    }
    if (argument == "--self-check" || argument == "--scenario" || argument == "--report" ||
        argument == "--verify" || argument == "--history") {
      options.command = argument.substr(2);
      continue;
    }
    if (argument == "--check") {
      options.command = "check";
    } else if (argument == "--ingest") {
      options.command = "ingest";
    } else if (argument == "--export-canonical") {
      options.command = "export-canonical";
    } else if (argument == "--export-interchange") {
      options.command = "export-interchange";
    } else if (argument == "--scope") {
      if (index + 1 >= argc) {
        return false;
      }
      options.scope = argv[++index];
      continue;
    } else if (argument == "--class") {
      if (index + 1 >= argc) {
        return false;
      }
      options.klass = argv[++index];
      continue;
    } else if (argument == "--medium") {
      if (index + 1 >= argc) {
        return false;
      }
      options.medium = argv[++index];
      continue;
    } else if (argument == "--accounted-at") {
      if (index + 1 >= argc) {
        return false;
      }
      options.accounted_at = argv[++index];
      continue;
    } else if (argument == "--seed") {
      if (index + 1 >= argc) {
        return false;
      }
      options.seed = std::strtoull(argv[++index], nullptr, 10);
      continue;
    } else if (argument == "--units") {
      if (index + 1 >= argc) {
        return false;
      }
      options.units = std::strtoll(argv[++index], nullptr, 10);
      options.has_units = true;
      continue;
    } else if (argument.rfind("--", 0) == 0) {
      std::cout << "unknown option " << argument << "\n";
      return false;
    }
    if (options.command == "check" || options.command == "ingest" ||
        options.command == "export-canonical" || options.command == "export-interchange") {
      if (index + 1 < argc && (argv[index + 1][0] != '-')) {
        options.file = argv[++index];
        continue;
      }
    }
    if (options.store.empty()) {
      options.store = argument;
      continue;
    }
    std::cout << "unexpected argument " << argument << "\n";
    return false;
  }
  if (options.command.empty()) {
    return false;
  }
  if (options.command == "ingest" && (options.store.empty() || options.file.empty())) {
    return false;
  }
  if (options.command == "check" && options.file.empty()) {
    return false;
  }
  if (options.command != "version" && options.command != "help" &&
      options.command != "check" && options.store.empty()) {
    return false;
  }
  return true;
}

[[nodiscard]] Result<std::string> read_text(const std::filesystem::path& path) {
  std::error_code code;
  if (!std::filesystem::exists(path, code)) {
    return Error::of(ErrorCode::StoreNotFound, "the file does not exist")
        .with_subject(path.string());
  }
  std::FILE* file = nullptr;
#ifdef _WIN32
  if (_wfopen_s(&file, path.wstring().c_str(), L"rb") != 0 || file == nullptr) {
    return Error::of(ErrorCode::IoFailure, "the file could not be opened")
        .with_subject(path.string());
  }
#else
  file = std::fopen(path.string().c_str(), "rb");
  if (file == nullptr) {
    return Error::of(ErrorCode::IoFailure, "the file could not be opened")
        .with_subject(path.string());
  }
#endif
  std::string text;
  char buffer[4096];
  std::size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    text.append(buffer, read);
  }
  static_cast<void>(std::fclose(file));
  return text;
}

[[nodiscard]] Result<void> write_binary(const std::filesystem::path& path,
                                        const std::vector<std::uint8_t>& bytes) {
  std::FILE* file = nullptr;
#ifdef _WIN32
  if (_wfopen_s(&file, path.wstring().c_str(), L"wb") != 0 || file == nullptr) {
    return Error::of(ErrorCode::IoFailure, "the file could not be created")
        .with_subject(path.string());
  }
#else
  file = std::fopen(path.string().c_str(), "wb");
  if (file == nullptr) {
    return Error::of(ErrorCode::IoFailure, "the file could not be created")
        .with_subject(path.string());
  }
#endif
  if (!bytes.empty() && std::fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size()) {
    static_cast<void>(std::fclose(file));
    return Error::of(ErrorCode::IoFailure, "the file could not be written")
        .with_subject(path.string());
  }
  static_cast<void>(std::fclose(file));
  return Ok{};
}

[[nodiscard]] Result<void> write_text(const std::filesystem::path& path,
                                      std::string_view text) {
  std::vector<std::uint8_t> bytes(text.begin(), text.end());
  return write_binary(path, bytes);
}

/// A deterministic synthetic evidence identifier for one contribution.
[[nodiscard]] EvidenceId synthetic_evidence_id(const std::string& unit) {
  return EvidenceId::from_validated(
      Identifier::from_validated("evidence." + unit));
}

/// Builds one SYNTHETIC facility generation. Nothing here is measured data.
[[nodiscard]] Result<AccountingInput> build_scenario(std::uint64_t seed, std::int64_t units,
                                                     Timestamp now) {
  if (units < 0 || units > 64) {
    return Error::of(ErrorCode::OutOfRange, "--units must be between 0 and 64");
  }
  AccountingInput input;
  input.generations = GenerationBundle::initial();
  input.policy = AccountingPolicy::baseline();
  input.policy.id = PolicyId::from_validated(Identifier::from_validated("cca.policy.cli"));
  input.policy.generation = input.generations.policy;
  input.policy.epoch = input.generations.epoch;

  EquipmentClass klass;
  klass.id = EquipmentClassId::from_validated(Identifier::from_validated("class.crah"));
  klass.kind = EquipmentClassKind::ComputerRoomAirHandler;
  klass.medium = Medium::Air;
  klass.label = BoundedText::from_validated("synthetic computer-room air handler");
  input.equipment_classes.push_back(klass);

  AccountingScope site;
  site.id = ScopeId::from_validated(Identifier::from_validated("site.synthetic"));
  site.kind = ScopeKind::Site;
  site.medium = Medium::Air;
  site.label = BoundedText::from_validated("synthetic site");
  site.topology_generation = input.generations.topology;
  site.epoch = input.generations.epoch;
  input.scopes.push_back(site);

  std::uint64_t state = seed == 0 ? 0x2545F4914F6CDD1DULL : seed;
  const auto next = [&state]() {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
  };

  for (int zone = 0; zone < 2; ++zone) {
    AccountingScope zone_scope;
    zone_scope.id = ScopeId::from_validated(
        Identifier::from_validated("zone." + std::to_string(zone)));
    zone_scope.kind = ScopeKind::Zone;
    zone_scope.parent = site.id;
    zone_scope.medium = Medium::Air;
    zone_scope.topology_generation = input.generations.topology;
    zone_scope.epoch = input.generations.epoch;
    input.scopes.push_back(zone_scope);

    AccountingScope loop_scope;
    loop_scope.id = ScopeId::from_validated(
        Identifier::from_validated("loop." + std::to_string(zone)));
    loop_scope.kind = ScopeKind::Loop;
    loop_scope.parent = zone_scope.id;
    loop_scope.medium = Medium::Air;
    loop_scope.topology_generation = input.generations.topology;
    loop_scope.epoch = input.generations.epoch;
    input.scopes.push_back(loop_scope);

    for (std::int64_t unit = 0; unit < units; ++unit) {
      const std::string name =
          "unit." + std::to_string(zone) + "." + std::to_string(unit);
      const EvidenceId evidence_id = synthetic_evidence_id(name);
      EvidenceRecord evidence;
      evidence.id = evidence_id;
      evidence.kind = EvidenceKind::Nameplate;
      evidence.source_kind = EvidenceSourceKind::SyntheticGenerator;
      evidence.source = EvidenceSourceId::from_validated(
          Identifier::from_validated("synthetic.generator"));
      evidence.label = BoundedText::from_validated("synthetic nameplate");
      evidence.reference = DocumentRef::parse("synthetic://nameplate/" + name).value();
      evidence.observed_at = now;
      evidence.recorded_at = now;
      evidence.binding = EvidenceBinding{input.generations.epoch,
                                         input.generations.topology,
                                         input.generations.policy,
                                         input.generations.evidence};
      evidence.medium = Medium::Air;
      evidence.declared_value =
          Measure<ThermalPower>::known(ThermalPower::of_milliwatts(250'000));
      evidence.content_digest = Digest::of_text(name);
      input.evidence.push_back(evidence);

      Contribution contribution;
      contribution.id = ContributionId::from_validated(Identifier::from_validated(name));
      contribution.home_scope = loop_scope.id;
      contribution.equipment =
          EquipmentId::from_validated(Identifier::from_validated("equipment." + name));
      contribution.equipment_class = klass.id;
      contribution.medium = Medium::Air;
      contribution.classification = ContributionClass::Additive;
      contribution.installed =
          Measure<ThermalPower>::known(ThermalPower::of_milliwatts(250'000));
      const std::uint64_t draw = next() % 10U;
      if (draw == 0U) {
        contribution.service = ServiceState::OutOfService;
      } else if (draw <= 2U) {
        contribution.service = ServiceState::Degraded;
        DerateFactor derate;
        derate.id = DerateId::from_validated(
            Identifier::from_validated("derate." + name));
        derate.kind = DerateKind::Factor;
        derate.factor = Ratio::of_ppm(800'000).value();
        derate.evidence = evidence_id;
        derate.label = BoundedText::from_validated("synthetic fouling derate");
        contribution.derates.push_back(derate);
      } else {
        contribution.service = ServiceState::InService;
      }
      contribution.binding = EvidenceBinding{input.generations.epoch,
                                             input.generations.topology,
                                             input.generations.policy,
                                             input.generations.evidence};
      contribution.primary_evidence = evidence_id;
      contribution.observed_at = now;
      input.contributions.push_back(contribution);
    }
  }

  CCA_TRY(AccountingLedger::validate_input(input, now));
  return input;
}

[[nodiscard]] Result<AccountingStore> open_store(const Options& options, bool read_only) {
  StoreOptions store_options;
  store_options.root = options.store;
  store_options.create_if_missing = !read_only;
  if (read_only) {
    return AccountingStore::open_read_only(store_options);
  }
  return AccountingStore::open(store_options);
}

[[nodiscard]] Result<int> report_store(const Options& options) {
  CCA_TRY_ASSIGN(store, open_store(options, true));
  CCA_TRY_ASSIGN(snapshot, store.latest());
  const AccountingLedger& ledger = snapshot.ledger();
  std::cout << "store " << options.store << "\n";
  std::cout << snapshot.summarize() << "\n";
  if (!options.scope.empty()) {
    CCA_TRY_ASSIGN(scope_id, ScopeId::parse(options.scope));
    CCA_TRY_ASSIGN(rollup, ledger.scope_rollup(scope_id));
    std::cout << "rollup scope " << rollup.root << " scopes="
              << rollup.totals.scope_count << " declared_mw="
              << rollup.totals.declared_installed.milliwatts() << " allocatable_mw="
              << rollup.totals.totals.allocatable.milliwatts() << "\n";
    for (const ScopeAccounting& record : rollup.scopes) {
      std::cout << "  " << record.scope.str() << " "
                << closure_status_name(record.status) << " declared_mw="
                << record.declared_installed.milliwatts() << " allocatable_mw="
                << record.totals.allocatable.milliwatts() << " withheld_mw="
                << record.totals.withheld.milliwatts() << " indeterminate_mw="
                << record.totals.indeterminate.milliwatts() << "\n";
    }
  } else if (!options.klass.empty()) {
    CCA_TRY_ASSIGN(class_id, EquipmentClassId::parse(options.klass));
    CCA_TRY_ASSIGN(rollup, ledger.class_rollup(class_id));
    std::cout << "rollup class " << rollup.root << " declared_mw="
              << rollup.totals.declared_installed.milliwatts() << " allocatable_mw="
              << rollup.totals.totals.allocatable.milliwatts() << "\n";
  } else if (!options.medium.empty()) {
    CCA_TRY_ASSIGN(medium, parse_medium(options.medium));
    CCA_TRY_ASSIGN(rollup, ledger.medium_rollup(medium));
    std::cout << "rollup medium " << rollup.root << " declared_mw="
              << rollup.totals.declared_installed.milliwatts() << " allocatable_mw="
              << rollup.totals.totals.allocatable.milliwatts() << "\n";
  } else {
    std::cout << ledger.to_report();
  }
  return kExitOk;
}

[[nodiscard]] Result<int> verify_store(const Options& options) {
  CCA_TRY_ASSIGN(store, open_store(options, true));
  CCA_TRY_ASSIGN(history, store.history());
  std::size_t failures = 0;
  for (const StoredGeneration& record : history) {
    CCA_TRY_ASSIGN(snapshot, store.load(record.generation));
    const Result<void> verified = snapshot.verify();
    if (!verified.ok()) {
      std::cout << "FAIL generation " << record.generation.value() << ": "
                << verified.error().to_string() << "\n";
      failures += 1;
      continue;
    }
    std::cout << "ok generation " << record.generation.value()
              << " commit_sequence=" << record.commit_sequence.value()
              << " digest=" << record.content_digest.to_hex()
              << (record.recovered ? " recovered" : "") << "\n";
  }
  if (failures != 0) {
    std::cout << "verification failed for " << failures << " generations\n";
    return kExitVerificationFailed;
  }
  std::cout << "store " << options.store << " verified: " << history.size()
            << " generations\n";
  return kExitOk;
}

[[nodiscard]] Result<int> run_self_check(const Options& options) {
  std::cout << version_banner() << "\n";
  std::cout << "SYNTHETIC self-check: no real facility data is involved\n";
  CCA_TRY_ASSIGN(now_value, parse_utc("2026-01-01T00:00:00.000Z"));
  const Timestamp now = now_value;
  CCA_TRY_ASSIGN(input, build_scenario(7U, 3, now));
  CCA_TRY_ASSIGN(ledger, AccountingLedger::build(input, now));
  const ClosureCheck closure = ledger.verify_closure();
  if (!closure.exact) {
    std::cout << "FAIL closure is not exact for " << closure.violations.size()
              << " scopes\n";
    for (const ClosureViolation& violation : closure.violations) {
      std::cout << "  " << violation.scope.str() << ": " << violation.message << "\n";
    }
    return kExitVerificationFailed;
  }
  for (const ScopeAccounting& record : ledger.scopes()) {
    const Result<void> identity = record.totals.verify(record.declared_installed);
    if (!identity.ok()) {
      std::cout << "FAIL identity for " << record.scope.str() << "\n";
      return kExitVerificationFailed;
    }
  }
  CCA_TRY_ASSIGN(canonical, encode_canonical(ledger.input()));
  CCA_TRY_ASSIGN(decoded, decode_canonical(canonical));
  CCA_TRY_ASSIGN(again, encode_canonical(decoded));
  if (canonical != again) {
    std::cout << "FAIL canonical bytes are not stable\n";
    return kExitVerificationFailed;
  }
  CCA_TRY_ASSIGN(text, write_interchange(ledger.input()));
  CCA_TRY_ASSIGN(reparsed, parse_interchange(text));
  CCA_TRY_ASSIGN(rendered, write_interchange(reparsed));
  if (text != rendered) {
    std::cout << "FAIL interchange round trip is not stable\n";
    return kExitVerificationFailed;
  }
  std::cout << "accounting digest " << ledger.digest().to_hex() << "\n";
  std::cout << ledger.to_report();

  StoreOptions store_options;
  store_options.root = options.store;
  store_options.create_if_missing = true;
  CCA_TRY_ASSIGN(store, AccountingStore::open(store_options));
  PublishRequest request;
  CCA_TRY_ASSIGN(attempt, AttemptId::from_material("self-check"));
  request.attempt = attempt;
  request.expected_previous = store.current_generation();
  request.expected_revision = input.generations.revision;
  request.epoch = store.fence().epoch;
  request.published_at = now;
  CCA_TRY_ASSIGN(snapshot, AccountingSnapshot::create(input, now));
  request.fingerprint = snapshot.content_digest();
  CCA_TRY_ASSIGN(outcome, store.publish(snapshot, request));
  CCA_TRY_ASSIGN(replayed, store.publish(snapshot, request));
  if (!replayed.replayed || replayed.record.generation != outcome.record.generation) {
    std::cout << "FAIL an identical retry did not replay the recorded outcome\n";
    return kExitVerificationFailed;
  }
  CCA_TRY_ASSIGN(loaded, store.latest());
  CCA_TRY(loaded.verify());
  std::cout << "published generation " << loaded.header().generation.value()
            << " into " << options.store << "\n";
  std::cout << "PASS\n";
  return kExitOk;
}

[[nodiscard]] Result<int> run_scenario(const Options& options) {
  std::cout << "SYNTHETIC scenario: no real facility data is involved\n";
  CCA_TRY_ASSIGN(now_value, parse_utc("2026-01-01T00:00:00.000Z"));
  const Timestamp now = now_value;
  CCA_TRY_ASSIGN(input, build_scenario(options.seed, options.units, now));
  EngineOptions engine_options;
  engine_options.root = options.store;
  engine_options.create_if_missing = true;
  CCA_TRY_ASSIGN(engine, AccountingEngine::open(engine_options));
  CCA_TRY_ASSIGN(snapshot,
                 engine.publish(input, now, now,
                                AttemptId::from_material("cli-scenario").value()));
  std::cout << snapshot.summarize() << "\n";
  std::cout << snapshot.ledger().to_report();
  return kExitOk;
}

[[nodiscard]] Result<int> run_check(const Options& options) {
  CCA_TRY_ASSIGN(text, read_text(std::filesystem::path(options.file)));
  CCA_TRY_ASSIGN(input, parse_interchange(text));
  CCA_TRY_ASSIGN(now_value, parse_utc("2026-01-01T00:00:00.000Z"));
  CCA_TRY(AccountingLedger::validate_input(input, now_value));
  CCA_TRY_ASSIGN(canonical, encode_canonical(input));
  CCA_TRY_ASSIGN(digest, canonical_digest(input));
  std::cout << "interchange accepted: scopes=" << input.scopes.size()
            << " contributions=" << input.contributions.size()
            << " evidence=" << input.evidence.size()
            << " canonical_bytes=" << canonical.size()
            << " digest=" << digest.to_hex() << "\n";
  return kExitOk;
}

[[nodiscard]] Result<int> run_ingest(const Options& options) {
  CCA_TRY_ASSIGN(text, read_text(std::filesystem::path(options.file)));
  CCA_TRY_ASSIGN(input, parse_interchange(text));
  Timestamp now = Timestamp::epoch();
  if (!options.accounted_at.empty()) {
    CCA_TRY_ASSIGN(parsed, parse_utc(options.accounted_at));
    now = parsed;
  } else {
    SystemClock clock;
    now = clock.now();
  }
  EngineOptions engine_options;
  engine_options.root = options.store;
  engine_options.create_if_missing = true;
  CCA_TRY_ASSIGN(engine, AccountingEngine::open(engine_options));
  CCA_TRY_ASSIGN(snapshot, engine.publish(input, now, now,
                                          AttemptId::from_material(options.file).value()));
  std::cout << snapshot.summarize() << "\n";
  return kExitOk;
}

[[nodiscard]] Result<int> run_export(const Options& options) {
  CCA_TRY_ASSIGN(store, open_store(options, true));
  CCA_TRY_ASSIGN(snapshot, store.latest());
  if (options.command == "export-canonical") {
    CCA_TRY(write_binary(std::filesystem::path(options.file),
                         snapshot.canonical_bytes()));
    std::cout << "wrote " << snapshot.canonical_bytes().size()
              << " canonical bytes to " << options.file << "\n";
    return kExitOk;
  }
  CCA_TRY_ASSIGN(text, write_interchange(snapshot.input()));
  CCA_TRY(write_text(std::filesystem::path(options.file), text));
  std::cout << "wrote interchange text to " << options.file << "\n";
  return kExitOk;
}

[[nodiscard]] Result<int> run_history(const Options& options) {
  CCA_TRY_ASSIGN(store, open_store(options, true));
  CCA_TRY_ASSIGN(history, store.history());
  for (const StoredGeneration& record : history) {
    std::cout << "generation " << record.generation.value()
              << " commit_sequence " << record.commit_sequence.value()
              << " accounted_at " << format_utc(record.accounted_at)
              << " published_at " << format_utc(record.published_at)
              << " payload_bytes " << record.payload_bytes
              << " digest " << record.content_digest.to_hex()
              << (record.recovered ? " recovered_from=" +
                                         std::to_string(record.recovered_from_generation)
                                   : "")
              << "\n";
  }
  if (history.empty()) {
    std::cout << "no published generation\n";
  }
  return kExitOk;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse_options(argc, argv, options)) {
    print_usage();
    return kExitUsage;
  }
  if (options.command == "help") {
    print_usage();
    return kExitOk;
  }
  if (options.command == "version") {
    std::cout << version_banner() << "\n";
    std::cout << "canonical format version " << kCanonicalFormatVersion
              << ", store format version " << kStoreFormatVersion
              << ", interchange format version " << kInterchangeFormatVersion << "\n";
    return kExitOk;
  }

  Result<int> outcome = Error::of(ErrorCode::NotSupported, "unknown command");
  if (options.command == "self-check") {
    outcome = run_self_check(options);
  } else if (options.command == "scenario") {
    outcome = run_scenario(options);
  } else if (options.command == "report") {
    outcome = report_store(options);
  } else if (options.command == "verify") {
    outcome = verify_store(options);
  } else if (options.command == "history") {
    outcome = run_history(options);
  } else if (options.command == "check") {
    outcome = run_check(options);
  } else if (options.command == "ingest") {
    outcome = run_ingest(options);
  } else if (options.command == "export-canonical" ||
             options.command == "export-interchange") {
    outcome = run_export(options);
  }
  if (!outcome.ok()) {
    std::cout << "error: " << outcome.error().to_string() << "\n";
    return kExitFailed;
  }
  return outcome.value();
}
