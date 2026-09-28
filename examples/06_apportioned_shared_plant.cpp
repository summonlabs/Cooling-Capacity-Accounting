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

// 06 - an apportioned shared plant
//
// Every facility fact used here is SYNTHETIC.


int main() {
  std::cout << "06 - apportioned shared plant (SYNTHETIC facility data)\n";
  example::AccountingInput input = example::make_input();
  input.equipment_classes.push_back(example::make_class(
      "class.chiller", example::Medium::Liquid, example::EquipmentClassKind::Chiller));
  input.scopes.push_back(example::make_scope("site.alpha", example::ScopeKind::Site, "",
                                             example::Medium::Liquid));
  input.scopes.push_back(example::make_scope("loop.north", example::ScopeKind::Loop,
                                             "site.alpha", example::Medium::Liquid));
  input.scopes.push_back(example::make_scope("loop.south", example::ScopeKind::Loop,
                                             "site.alpha", example::Medium::Liquid));

  input.evidence.push_back(example::make_evidence("evidence.plant", 1'000'000));
  example::Contribution plant = example::make_unit("unit.plant", "loop.north",
                                                   "class.chiller", 1'000'000,
                                                   "evidence.plant");
  plant.medium = example::Medium::Liquid;
  // The plant is homed in loop.north. 40% is apportioned to loop.south and the
  // remaining 60% stays in the home scope, where the unapportioned part is
  // reported rather than silently distributed. A share may never target the
  // home scope itself: that would count the same mass twice.
  plant.sharing.kind = example::Sharing::Kind::Apportioned;
  plant.sharing.shares.push_back(example::ApportionmentShare{
      example::id_of<example::ScopeId>("loop.south"),
      example::Ratio::of_ppm(400'000).value()});
  input.contributions.push_back(plant);

  EXAMPLE_ASSIGN(ledger, example::AccountingLedger::build(input, example::now()));
  example::print_scopes(ledger);
  example::print_findings(ledger);
  if (!example::check_closure(ledger)) {
    return 1;
  }

  EXAMPLE_ASSIGN(north, ledger.scope_rollup(example::id_of<example::ScopeId>(
                            "loop.north")));
  EXAMPLE_ASSIGN(south, ledger.scope_rollup(example::id_of<example::ScopeId>(
                            "loop.south")));
  const std::int64_t north_mw = north.scopes.front().declared_installed.milliwatts();
  const std::int64_t south_mw = south.scopes.front().declared_installed.milliwatts();
  std::cout << "  north " << north_mw << "mW (of which " << (north_mw - 400'000)
            << "mW is the unapportioned remainder), south " << south_mw << "mW\n";
  EXAMPLE_REQUIRE(south_mw == 400'000, "the southern loop receives exactly its share");
  EXAMPLE_REQUIRE(north_mw == 600'000,
                  "the home scope keeps the rest, including the unapportioned part");
  EXAMPLE_REQUIRE(north.scopes.front().declared_installed.milliwatts() +
                      south.scopes.front().declared_installed.milliwatts() ==
                      1'000'000,
                  "the parts and the remainder re-sum to the whole contribution");
  bool saw_unapportioned = false;
  for (const example::Finding& finding : ledger.findings()) {
    if (finding.code == example::FindingCode::UnapportionedShare) {
      saw_unapportioned = true;
    }
  }
  EXAMPLE_REQUIRE(saw_unapportioned,
                  "the unapportioned remainder is reported, never distributed "
                  "silently");
  std::cout << "  OK\n";
  return 0;
}
