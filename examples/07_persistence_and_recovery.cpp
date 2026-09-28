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

#include <cstdlib>
#include <iostream>

#include "example_support.hpp"

// 07 - persistence, recovery and revalidation
//
// Every facility fact used here is SYNTHETIC.


namespace {

[[nodiscard]] example::AccountingInput build_revision(std::uint64_t revision,
                                                      std::int64_t second_unit_mw) {
  example::AccountingInput input = example::make_input();
  const example::Result<example::StateRevision> revision_value =
      example::StateRevision::of(revision);
  if (!revision_value.ok()) {
    std::cout << "  invalid revision " << revision << "\n";
    std::exit(2);
  }
  input.generations.revision = revision_value.value();
  input.equipment_classes.push_back(example::make_class(
      "class.crah", example::Medium::Air,
      example::EquipmentClassKind::ComputerRoomAirHandler));
  input.scopes.push_back(example::make_scope("site.alpha", example::ScopeKind::Site, "",
                                             example::Medium::Air));
  input.scopes.push_back(example::make_scope("loop.a", example::ScopeKind::Loop,
                                             "site.alpha", example::Medium::Air));
  input.evidence.push_back(example::make_evidence("evidence.one", 250'000));
  input.evidence.push_back(example::make_evidence("evidence.two", second_unit_mw));
  input.contributions.push_back(example::make_unit("unit.one", "loop.a", "class.crah",
                                                   250'000, "evidence.one"));
  input.contributions.push_back(example::make_unit("unit.two", "loop.a", "class.crah",
                                                   second_unit_mw, "evidence.two"));
  return input;
}

}  // namespace

int main() {
  std::cout << "07 - persistence, recovery and revalidation (SYNTHETIC data)\n";
  example::Workspace workspace("07-persistence");
  std::cout << "  store root " << workspace.path().string() << "\n";

  example::EngineOptions options;
  options.root = workspace.path();
  options.create_if_missing = true;

  EXAMPLE_ASSIGN(engine, example::AccountingEngine::open(options));
  EXAMPLE_ASSIGN(status, engine.status());
  std::cout << "  fresh store: has_generation="
            << (status.has_generation ? "true" : "false") << " generation "
            << status.generation.value() << "\n";
  EXAMPLE_REQUIRE(!status.has_generation, "a fresh store holds no generation");

  EXAMPLE_ASSIGN(first,
                 engine.publish(build_revision(1, 250'000), example::now(),
                                example::now(),
                                example::AttemptId::from_material("attempt-1").value()));
  EXAMPLE_ASSIGN(second,
                 engine.publish(build_revision(2, 400'000), example::now(),
                                example::now(),
                                example::AttemptId::from_material("attempt-2").value()));
  std::cout << "  published generation " << first.header().generation.value()
            << " then " << second.header().generation.value() << "\n";
  EXAMPLE_REQUIRE(second.header().generation.value() == 2,
                  "the second publication advances the generation");

  // Every retained generation loads again and re-accounts to the digest it was
  // published with.
  EXAMPLE_ASSIGN(reloaded, engine.load(example::AccountGeneration::of(2).value()));
  EXAMPLE_REQUIRE(reloaded.content_digest() == second.content_digest(),
                  "a reloaded generation has the digest it was published with");
  EXAMPLE_REQUIRE(reloaded.verify().ok(),
                  "a reloaded generation re-accounts to the same digest");

  EXAMPLE_ASSIGN(history, engine.history());
  std::cout << "  history holds " << history.size() << " generations\n";
  for (const example::StoredGeneration& record : history) {
    std::cout << "    generation " << record.generation.value() << " digest "
              << record.content_digest.to_hex().substr(0, 16) << "...\n";
  }
  EXAMPLE_REQUIRE(history.size() == 2, "both publications are retained");

  EXAMPLE_REQUIRE(engine.close().ok(), "closing the engine succeeds");

  // Reopening recovers the authoritative generation, and recovered state is not
  // fresh physical evidence: the engine says so until it is revalidated.
  EXAMPLE_ASSIGN(reopened, example::AccountingEngine::open(options));
  EXAMPLE_ASSIGN(reopened_status, reopened.status());
  std::cout << "  reopened: generation " << reopened_status.generation.value()
            << ", recovered pending revalidation="
            << (reopened_status.recovered_pending_revalidation ? "true" : "false")
            << "\n";
  EXAMPLE_REQUIRE(reopened_status.generation.value() == 2,
                  "the store resolves to exactly the last committed generation");
  EXAMPLE_REQUIRE(reopened_status.recovered_pending_revalidation,
                  "recovered state is marked as pending revalidation");

  EXAMPLE_ASSIGN(current, reopened.current());
  EXAMPLE_REQUIRE(current.header().recovered,
                  "the recovered generation carries its recovered marker");
  EXAMPLE_REQUIRE(current.verify().ok(),
                  "the recovered generation re-accounts to the same digest");

  EXAMPLE_ASSIGN(revalidated,
                 reopened.revalidate(build_revision(3, 400'000), example::now(),
                                     example::now(),
                                     example::AttemptId::from_material("attempt-3").value()));
  std::cout << "  revalidated into generation "
            << revalidated.header().generation.value() << " recovered="
            << (revalidated.header().recovered ? "true" : "false") << "\n";
  EXAMPLE_REQUIRE(!revalidated.header().recovered,
                  "a revalidated generation is fresh again");
  EXAMPLE_REQUIRE(!reopened.recovered_pending_revalidation(),
                  "the pending mark is cleared by revalidation");

  EXAMPLE_ASSIGN(final_status, reopened.status());
  EXAMPLE_ASSIGN(final_history, reopened.history());
  EXAMPLE_REQUIRE(final_history.size() == 3, "all three generations are retained");
  static_cast<void>(final_status);
  std::cout << "  OK\n";
  return 0;
}
