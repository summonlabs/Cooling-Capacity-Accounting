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

// Completed-operation benchmarks for Cooling Capacity Accounting.
//
// Every workload runs to completion before its time is recorded: an accounting
// generation is fully accounted, a durable publication has been flushed to
// stable storage, read back and committed by the manifest rename. Submission
// latency is never measured, because nothing here is queued.
//
// The facility data is SYNTHETIC. The timing, the filesystem work and the
// process behaviour are REAL.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

namespace {

using namespace cooling_capacity_accounting;

constexpr std::int64_t kNowMilliseconds = 1'767'225'600'000LL;

[[nodiscard]] Timestamp bench_now() {
  return Timestamp::of_unix_milliseconds(kNowMilliseconds);
}

template <typename Id>
[[nodiscard]] Id id_of(std::string_view text) {
  return Id::from_validated(Identifier::from_validated(std::string(text)));
}

/// A synthetic facility with a exact number of contributions, spread over zones
/// and loops so the rollups have real structure to aggregate.
[[nodiscard]] AccountingInput synthetic_facility(std::size_t contributions) {
  AccountingInput input;
  input.generations = GenerationBundle::initial();
  input.policy = AccountingPolicy::baseline();
  input.policy.id = id_of<PolicyId>("cca.policy.benchmark");
  input.policy.generation = input.generations.policy;
  input.policy.epoch = input.generations.epoch;
  input.policy.require_installed_evidence = false;

  EquipmentClass klass;
  klass.id = id_of<EquipmentClassId>("class.crah");
  klass.kind = EquipmentClassKind::ComputerRoomAirHandler;
  klass.medium = Medium::Air;
  klass.label = BoundedText::from_validated("synthetic unit");
  input.equipment_classes.push_back(klass);

  AccountingScope site;
  site.id = id_of<ScopeId>("site.benchmark");
  site.kind = ScopeKind::Site;
  site.medium = Medium::Air;
  site.topology_generation = input.generations.topology;
  site.epoch = input.generations.epoch;
  input.scopes.push_back(site);

  constexpr std::size_t kZones = 8;
  constexpr std::size_t kLoopsPerZone = 4;
  for (std::size_t zone = 0; zone < kZones; ++zone) {
    AccountingScope zone_scope;
    zone_scope.id = id_of<ScopeId>("zone." + std::to_string(zone));
    zone_scope.kind = ScopeKind::Zone;
    zone_scope.parent = site.id;
    zone_scope.medium = Medium::Air;
    zone_scope.topology_generation = input.generations.topology;
    zone_scope.epoch = input.generations.epoch;
    input.scopes.push_back(zone_scope);
    for (std::size_t loop = 0; loop < kLoopsPerZone; ++loop) {
      AccountingScope loop_scope;
      loop_scope.id = id_of<ScopeId>("loop." + std::to_string(zone) + "." +
                                     std::to_string(loop));
      loop_scope.kind = ScopeKind::Loop;
      loop_scope.parent = zone_scope.id;
      loop_scope.medium = Medium::Air;
      loop_scope.topology_generation = input.generations.topology;
      loop_scope.epoch = input.generations.epoch;
      input.scopes.push_back(loop_scope);
    }
  }

  for (std::size_t index = 0; index < contributions; ++index) {
    const std::size_t zone = index % kZones;
    const std::size_t loop = (index / kZones) % kLoopsPerZone;
    Contribution contribution;
    contribution.id = id_of<ContributionId>("unit." + std::to_string(index));
    contribution.home_scope =
        id_of<ScopeId>("loop." + std::to_string(zone) + "." + std::to_string(loop));
    contribution.equipment =
        id_of<EquipmentId>("equipment." + std::to_string(index));
    contribution.equipment_class = klass.id;
    contribution.medium = Medium::Air;
    contribution.classification = ContributionClass::Additive;
    contribution.installed =
        Measure<ThermalPower>::known(ThermalPower::of_milliwatts(250'000));
    contribution.service =
        index % 7U == 0U ? ServiceState::OutOfService
                         : (index % 5U == 0U ? ServiceState::Degraded
                                             : ServiceState::InService);
    if (contribution.service == ServiceState::Degraded) {
      DerateFactor derate;
      derate.id = id_of<DerateId>("derate." + std::to_string(index));
      derate.kind = DerateKind::Factor;
      derate.factor = Ratio::of_ppm(900'000).value();
      contribution.derates.push_back(derate);
    }
    contribution.binding = EvidenceBinding{
        input.generations.epoch, input.generations.topology, input.generations.policy,
        input.generations.evidence};
    contribution.observed_at = bench_now();
    input.contributions.push_back(contribution);
  }
  return input;
}

struct Samples {
  std::vector<double> microseconds;
};

[[nodiscard]] double percentile(const std::vector<double>& sorted, double fraction) {
  if (sorted.empty()) {
    return 0.0;
  }
  const double position = fraction * static_cast<double>(sorted.size() - 1U);
  const auto index = static_cast<std::size_t>(position + 0.5);
  return sorted[std::min(index, sorted.size() - 1U)];
}

void report(const std::string& name, const std::string& unit, std::size_t iterations,
            const Samples& samples) {
  std::vector<double> sorted = samples.microseconds;
  std::sort(sorted.begin(), sorted.end());
  double total = 0.0;
  for (const double value : sorted) {
    total += value;
  }
  const double mean = sorted.empty() ? 0.0 : total / static_cast<double>(sorted.size());
  std::cout << name << " | iterations=" << iterations << " | unit=" << unit
            << " | min_us=" << sorted.front() << " | median_us="
            << percentile(sorted, 0.5) << " | p95_us=" << percentile(sorted, 0.95)
            << " | max_us=" << sorted.back() << " | mean_us=" << mean << "\n";
}

template <typename Operation>
[[nodiscard]] Samples measure(std::size_t warmup, std::size_t iterations,
                              Operation operation) {
  for (std::size_t index = 0; index < warmup; ++index) {
    operation();
  }
  Samples samples;
  samples.microseconds.reserve(iterations);
  for (std::size_t index = 0; index < iterations; ++index) {
    const auto started = std::chrono::steady_clock::now();
    operation();
    const auto finished = std::chrono::steady_clock::now();
    samples.microseconds.push_back(
        std::chrono::duration<double, std::micro>(finished - started).count());
  }
  return samples;
}

[[nodiscard]] std::filesystem::path make_workspace(const std::string& label) {
  std::error_code code;
  std::filesystem::path base = std::filesystem::temp_directory_path(code);
  if (code) {
    base = std::filesystem::current_path();
  }
  const std::filesystem::path path =
      base / ("cca-benchmark-" + label + "-" +
              std::to_string(static_cast<unsigned long long>(
                  std::chrono::steady_clock::now().time_since_epoch().count())));
  std::error_code remove_code;
  static_cast<void>(std::filesystem::remove_all(path, remove_code));
  std::error_code create_code;
  static_cast<void>(std::filesystem::create_directories(path, create_code));
  return path;
}

}  // namespace

int main(int argc, char** argv) {
  bool quick = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--quick") {
      quick = true;
    } else {
      std::cout << "usage: cca_benchmark [--quick]\n";
      return 1;
    }
  }
  const std::size_t warmup = quick ? 1U : 3U;
  const std::size_t iterations = quick ? 3U : 20U;
  const std::size_t sizes[3] = {64U, 512U, 2048U};

  std::cout << "cca_benchmark " << version_string() << "\n";
  std::cout << "workload: SYNTHETIC facility data, REAL accounting, REAL durable "
               "storage on the local filesystem\n";
  std::cout << "method: warmup=" << warmup << " iterations=" << iterations
            << " (each iteration runs one operation to completion)\n";
  std::cout << "environment: " << (quick ? "quick smoke profile" : "full profile")
            << ", single process, no other workload driven by this tool\n";

  for (const std::size_t size : sizes) {
    const AccountingInput input = synthetic_facility(size);
    const std::string label = std::to_string(size);

    const Samples build = measure(warmup, iterations, [&input]() {
      const Result<AccountingLedger> ledger = AccountingLedger::build(input, bench_now());
      if (!ledger.ok()) {
        std::cout << "ledger build failed\n";
        std::exit(2);
      }
    });
    report("ledger_build_" + label, "one completed accounting generation", iterations,
           build);

    Result<AccountingLedger> ledger = AccountingLedger::build(input, bench_now());
    if (!ledger.ok()) {
      std::cout << "ledger build failed\n";
      return 2;
    }
    const ScopeId root = id_of<ScopeId>("site.benchmark");
    const Samples rollup = measure(warmup, iterations, [&ledger, &root]() {
      const Result<RollupView> view = ledger.value().scope_rollup(root);
      if (!view.ok()) {
        std::cout << "rollup failed\n";
        std::exit(2);
      }
    });
    report("site_rollup_" + label, "one completed site rollup", iterations, rollup);

    const Samples encode = measure(warmup, iterations, [&input]() {
      const Result<std::vector<std::uint8_t>> bytes = encode_canonical(input);
      if (!bytes.ok()) {
        std::cout << "encode failed\n";
        std::exit(2);
      }
    });
    report("canonical_encode_" + label, "one completed canonical encoding", iterations,
           encode);

    Result<std::vector<std::uint8_t>> bytes = encode_canonical(input);
    if (!bytes.ok()) {
      std::cout << "encode failed\n";
      return 2;
    }
    const Samples decode = measure(warmup, iterations, [&bytes]() {
      const Result<AccountingInput> decoded = decode_canonical(bytes.value());
      if (!decoded.ok()) {
        std::cout << "decode failed\n";
        std::exit(2);
      }
    });
    report("canonical_decode_" + label, "one completed canonical decoding", iterations,
           decode);

    // Durable publication: write, flush, read back, rename, commit the manifest.
    const std::filesystem::path root_path = make_workspace("publish-" + label);
    StoreOptions store_options;
    store_options.root = root_path;
    store_options.create_if_missing = true;
    Result<AccountingStore> store = AccountingStore::open(store_options);
    if (!store.ok()) {
      std::cout << "store open failed: " << store.error().to_string() << "\n";
      return 2;
    }
    std::size_t sequence = 0;
    const Samples publish = measure(warmup, iterations, [&]() {
      Result<AccountingSnapshot> snapshot =
          AccountingSnapshot::create(input, bench_now());
      if (!snapshot.ok()) {
        std::cout << "snapshot failed\n";
        std::exit(2);
      }
      SnapshotHeader header = snapshot.value().header();
      const Result<StateRevision> revision = StateRevision::of(sequence);
      if (!revision.ok()) {
        std::cout << "revision failed\n";
        std::exit(2);
      }
      header.generations.revision = revision.value();
      Result<AccountingSnapshot> revised = AccountingSnapshot::from_canonical(
          snapshot.value().canonical_bytes(), header);
      if (!revised.ok()) {
        std::cout << "snapshot rebuild failed\n";
        std::exit(2);
      }
      PublishRequest request;
      const Result<AttemptId> attempt =
          AttemptId::from_material("benchmark-" + std::to_string(sequence));
      if (!attempt.ok()) {
        std::cout << "attempt failed\n";
        std::exit(2);
      }
      request.attempt = attempt.value();
      request.fingerprint = revised.value().content_digest();
      request.expected_previous = store.value().current_generation();
      request.expected_revision = header.generations.revision;
      request.epoch = store.value().fence().epoch;
      request.published_at = bench_now();
      const Result<PublishOutcome> outcome = store.value().publish(revised.value(), request);
      if (!outcome.ok()) {
        std::cout << "publish failed: " << outcome.error().to_string() << "\n";
        std::exit(2);
      }
      sequence += 1;
    });
    report("publish_durable_" + label,
           "one completed durable publication (flush, read-back, commit)", iterations,
           publish);

    const Result<std::vector<StoredGeneration>> retained = store.value().history();
    if (retained.ok()) {
      std::cout << "retained_generations_" << label
                << " | count=" << retained.value().size() << "\n";
    }
    const Result<void> closed = store.value().close();
    static_cast<void>(closed);
    std::error_code remove_code;
    static_cast<void>(std::filesystem::remove_all(root_path, remove_code));
  }

  std::cout << "SYNTHETIC facility data; the accounting, the encoding and the durable "
               "publication are REAL work performed by this process.\n";
  return 0;
}
