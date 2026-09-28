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

// An independent downstream consumer. It is a standalone CMake project, it is
// not part of the Cooling Capacity Accounting build, and it configures only
// against an installed prefix through find_package. It exercises a real library
// lifecycle: account a generation, publish it durably, reopen the store from a
// fresh object, verify the generation and check the accounting identity.
//
// The facility data is SYNTHETIC.

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include <cooling_capacity_accounting/cooling_capacity_accounting.hpp>

namespace {

using namespace cooling_capacity_accounting;

int fail(const std::string& message) {
  std::cout << "consumer FAILED: " << message << "\n";
  return 1;
}

template <typename Id>
[[nodiscard]] Id id_of(const char* text) {
  return Id::from_validated(Identifier::from_validated(text));
}

[[nodiscard]] AccountingInput synthetic_input() {
  AccountingInput input;
  input.generations = GenerationBundle::initial();
  input.policy = AccountingPolicy::baseline();
  input.policy.id = id_of<PolicyId>("consumer.policy");
  input.policy.generation = input.generations.policy;
  input.policy.epoch = input.generations.epoch;
  input.policy.require_installed_evidence = false;

  EquipmentClass klass;
  klass.id = id_of<EquipmentClassId>("class.consumer");
  klass.kind = EquipmentClassKind::ComputerRoomAirHandler;
  klass.medium = Medium::Air;
  klass.label = BoundedText::from_validated("consumer unit");
  input.equipment_classes.push_back(klass);

  AccountingScope site;
  site.id = id_of<ScopeId>("site.consumer");
  site.kind = ScopeKind::Site;
  site.medium = Medium::Air;
  site.topology_generation = input.generations.topology;
  site.epoch = input.generations.epoch;
  input.scopes.push_back(site);

  AccountingScope loop;
  loop.id = id_of<ScopeId>("loop.consumer");
  loop.kind = ScopeKind::Loop;
  loop.parent = site.id;
  loop.medium = Medium::Air;
  loop.topology_generation = input.generations.topology;
  loop.epoch = input.generations.epoch;
  input.scopes.push_back(loop);

  for (int index = 0; index < 4; ++index) {
    Contribution contribution;
    contribution.id =
        id_of<ContributionId>(std::string("unit." + std::to_string(index)).c_str());
    contribution.home_scope = loop.id;
    contribution.equipment =
        id_of<EquipmentId>(std::string("equipment." + std::to_string(index)).c_str());
    contribution.equipment_class = klass.id;
    contribution.medium = Medium::Air;
    contribution.classification = ContributionClass::Additive;
    contribution.installed =
        Measure<ThermalPower>::known(ThermalPower::of_milliwatts(250'000));
    contribution.service = index == 3 ? ServiceState::OutOfService
                                      : ServiceState::InService;
    contribution.binding =
        EvidenceBinding{input.generations.epoch, input.generations.topology,
                        input.generations.policy, input.generations.evidence};
    contribution.observed_at = Timestamp::of_unix_milliseconds(1'767'225'600'000LL);
    input.contributions.push_back(contribution);
  }
  return input;
}

}  // namespace

int main() {
  std::cout << "downstream consumer of " << version_banner() << "\n";

  const Timestamp now = Timestamp::of_unix_milliseconds(1'767'225'600'000LL);
  const AccountingInput input = synthetic_input();

  const Result<AccountingLedger> ledger = AccountingLedger::build(input, now);
  if (!ledger.ok()) {
    return fail("AccountingLedger::build: " + ledger.error().to_string());
  }
  const ClosureCheck closure = ledger.value().verify_closure();
  if (!closure.exact) {
    return fail("the accounting identity does not hold");
  }
  std::cout << "  accounted: declared "
            << ledger.value()
                   .overall_totals()
                   .declared_installed.milliwatts()
            << "mW, allocatable "
            << ledger.value().overall_totals().totals.allocatable.milliwatts()
            << "mW over " << ledger.value().scopes().size() << " scopes\n";

  std::error_code code;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path(code) / "cca-package-consumer-store";
  static_cast<void>(std::filesystem::remove_all(root, code));

  {
    StoreOptions options;
    options.root = root;
    options.create_if_missing = true;
    Result<AccountingStore> store = AccountingStore::open(options);
    if (!store.ok()) {
      return fail("AccountingStore::open: " + store.error().to_string());
    }
    const Result<AccountingSnapshot> snapshot = AccountingSnapshot::create(input, now);
    if (!snapshot.ok()) {
      return fail("AccountingSnapshot::create: " + snapshot.error().to_string());
    }
    PublishRequest request;
    const Result<AttemptId> attempt = AttemptId::from_material("consumer-attempt");
    if (!attempt.ok()) {
      return fail("AttemptId::from_material");
    }
    request.attempt = attempt.value();
    request.fingerprint = snapshot.value().content_digest();
    request.expected_previous = store.value().current_generation();
    request.expected_revision = snapshot.value().header().generations.revision;
    request.epoch = store.value().fence().epoch;
    request.published_at = now;
    const Result<PublishOutcome> outcome = store.value().publish(snapshot.value(), request);
    if (!outcome.ok()) {
      return fail("AccountingStore::publish: " + outcome.error().to_string());
    }
    if (outcome.value().record.generation.value() != 1U) {
      return fail("the first publication must be generation 1");
    }
    const Result<PublishOutcome> replay = store.value().publish(snapshot.value(), request);
    if (!replay.ok() || !replay.value().replayed) {
      return fail("an identical retry must replay the recorded outcome");
    }
    const Result<void> closed = store.value().close();
    if (!closed.ok()) {
      return fail("AccountingStore::close");
    }
  }

  // Reopen from a fresh object: the committed generation must come back whole.
  StoreOptions options;
  options.root = root;
  Result<AccountingStore> reopened = AccountingStore::open_read_only(options);
  if (!reopened.ok()) {
    return fail("reopen: " + reopened.error().to_string());
  }
  const Result<AccountingSnapshot> loaded = reopened.value().latest();
  if (!loaded.ok()) {
    return fail("latest: " + loaded.error().to_string());
  }
  if (!loaded.value().verify().ok()) {
    return fail("the reloaded generation does not verify");
  }
  if (!loaded.value().ledger().verify_closure().exact) {
    return fail("the reloaded generation does not close");
  }
  std::cout << "  published generation "
            << loaded.value().header().generation.value() << " digest "
            << loaded.value().content_digest().to_hex() << "\n";
  const Result<void> closed_again = reopened.value().close();
  static_cast<void>(closed_again);
  static_cast<void>(std::filesystem::remove_all(root, code));
  std::cout << "consumer OK\n";
  return 0;
}
