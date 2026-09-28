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

#include <iostream>

#include "example_support.hpp"

// 03 - derates and degradation
//
// Every facility fact used here is SYNTHETIC.


int main() {
  std::cout << "03 - derates and degradation (SYNTHETIC facility data)\n";
  example::AccountingInput input = example::make_input();
  input.equipment_classes.push_back(example::make_class(
      "class.chiller", example::Medium::Liquid,
      example::EquipmentClassKind::Chiller));
  input.scopes.push_back(example::make_scope("site.alpha", example::ScopeKind::Site, "",
                                             example::Medium::Liquid));
  input.scopes.push_back(example::make_scope("loop.chilled", example::ScopeKind::Loop,
                                             "site.alpha", example::Medium::Liquid));

  constexpr std::int64_t kInstalled = 1'000'000;  // 1 kW, for readable arithmetic
  input.evidence.push_back(example::make_evidence("evidence.chiller", kInstalled));
  example::Contribution chiller =
      example::make_unit("unit.chiller", "loop.chilled", "class.chiller", kInstalled,
                         "evidence.chiller");
  chiller.medium = example::Medium::Liquid;
  chiller.service = example::ServiceState::Degraded;
  // The baseline policy requires evidence for every derate: an unevidenced
  // reduction cannot support an allocatable claim, so the contribution would
  // become indeterminate instead.
  example::DerateFactor altitude(example::id_of<example::DerateId>("derate.altitude"),
                                 example::Ratio::of_ppm(950'000).value());
  altitude.evidence = example::id_of<example::EvidenceId>("evidence.chiller");
  chiller.derates.push_back(altitude);
  example::DerateFactor coolant(example::id_of<example::DerateId>("derate.coolant"),
                                example::Ratio::of_ppm(900'000).value());
  coolant.evidence = example::id_of<example::EvidenceId>("evidence.chiller");
  chiller.derates.push_back(coolant);
  input.contributions.push_back(chiller);

  EXAMPLE_ASSIGN(ledger, example::AccountingLedger::build(input, example::now()));
  example::print_scopes(ledger);
  if (!example::check_closure(ledger)) {
    return 1;
  }

  // 1000000 * 950000/1000000 * 900000/1000000 = 855000 exactly.
  EXAMPLE_ASSIGN(loop_cell,
                 ledger.scope_rollup(example::id_of<example::ScopeId>("loop.chilled")));
  const example::ScopeAccounting& scope = loop_cell.scopes.front();
  std::cout << "  two factors compose exactly: retained "
            << scope.totals.allocatable.milliwatts() << "mW, lost "
            << scope.totals.degraded_loss.milliwatts() << "mW\n";
  EXAMPLE_REQUIRE(scope.totals.allocatable.milliwatts() == 855'000,
                  "stacked factor derates must compose exactly");
  EXAMPLE_REQUIRE(scope.totals.degraded_loss.milliwatts() == 145'000,
                  "the loss is the difference between installed and retained");

  // The order the factors were supplied in must not matter.
  example::AccountingInput reversed = input;
  std::swap(reversed.contributions.front().derates[0],
            reversed.contributions.front().derates[1]);
  EXAMPLE_ASSIGN(reversed_ledger,
                 example::AccountingLedger::build(reversed, example::now()));
  EXAMPLE_ASSIGN(reversed_cell, reversed_ledger.scope_rollup(
                                    example::id_of<example::ScopeId>("loop.chilled")));
  std::cout << "  reversed factor order gives "
            << reversed_cell.scopes.front().totals.allocatable.milliwatts() << "mW\n";
  EXAMPLE_REQUIRE(reversed_cell.scopes.front().totals.allocatable.milliwatts() ==
                      scope.totals.allocatable.milliwatts(),
                  "derate composition must not depend on submission order");
  EXAMPLE_REQUIRE(reversed_ledger.digest() == ledger.digest(),
                  "the accounting digest must not depend on submission order");

  // An absolute reduction is subtracted before the factors apply.
  example::AccountingInput absolute = input;
  absolute.contributions.front().derates.clear();
  example::DerateFactor fouling(example::id_of<example::DerateId>("derate.fouling"),
                               example::ThermalPower::of_milliwatts(100'000));
  fouling.evidence = example::id_of<example::EvidenceId>("evidence.chiller");
  absolute.contributions.front().derates.push_back(fouling);
  example::DerateFactor load(example::id_of<example::DerateId>("derate.load"),
                             example::Ratio::of_ppm(500'000).value());
  load.evidence = example::id_of<example::EvidenceId>("evidence.chiller");
  absolute.contributions.front().derates.push_back(load);
  EXAMPLE_ASSIGN(absolute_ledger,
                 example::AccountingLedger::build(absolute, example::now()));
  EXAMPLE_ASSIGN(absolute_cell, absolute_ledger.scope_rollup(
                                    example::id_of<example::ScopeId>("loop.chilled")));
  std::cout << "  absolute then factor gives "
            << absolute_cell.scopes.front().totals.allocatable.milliwatts() << "mW\n";
  EXAMPLE_REQUIRE(absolute_cell.scopes.front().totals.allocatable.milliwatts() ==
                      450'000,
                  "the absolute reduction is applied before the factors");
  std::cout << "  OK\n";
  return 0;
}
