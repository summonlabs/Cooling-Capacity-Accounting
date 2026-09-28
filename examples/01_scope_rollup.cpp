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

// 01 - scope rollups.
//
// A synthetic site holds two zones, each with one loop and two 250 kW units.
// The example accounts the generation, prints every scope cell, then rolls the
// same cells up by subtree, by equipment class and by medium. All of that data
// is SYNTHETIC.

int main() {
  std::cout << "01 - scope rollups (SYNTHETIC facility data)\n";
  example::AccountingInput input = example::make_input();
  input.equipment_classes.push_back(example::make_class(
      "class.crah", example::Medium::Air,
      example::EquipmentClassKind::ComputerRoomAirHandler));

  input.scopes.push_back(example::make_scope("site.alpha", example::ScopeKind::Site, "",
                                             example::Medium::Air));
  for (int zone = 0; zone < 2; ++zone) {
    const std::string zone_id = "zone." + std::to_string(zone);
    const std::string loop_id = "loop." + std::to_string(zone);
    input.scopes.push_back(
        example::make_scope(zone_id, example::ScopeKind::Zone, "site.alpha",
                            example::Medium::Air));
    input.scopes.push_back(
        example::make_scope(loop_id, example::ScopeKind::Loop, zone_id,
                            example::Medium::Air));
    for (int unit = 0; unit < 2; ++unit) {
      const std::string name = "unit." + std::to_string(zone) + "." +
                               std::to_string(unit);
      input.evidence.push_back(example::make_evidence("evidence." + name, 250'000));
      input.contributions.push_back(example::make_unit(
          name, loop_id, "class.crah", 250'000, "evidence." + name));
    }
  }

  EXAMPLE_ASSIGN(ledger,
                 example::AccountingLedger::build(input, example::now()));
  std::cout << "  accounted at " << example::format_utc(ledger.accounted_at()) << "\n";
  example::print_scopes(ledger);
  if (!example::check_closure(ledger)) {
    return 1;
  }

  EXAMPLE_ASSIGN(site_rollup,
                 ledger.scope_rollup(example::id_of<example::ScopeId>("site.alpha")));
  std::cout << "  rollup site.alpha covers " << site_rollup.totals.scope_count
            << " scopes, allocatable "
            << site_rollup.totals.totals.allocatable.milliwatts() << "mW\n";

  EXAMPLE_ASSIGN(zone_rollup,
                 ledger.scope_rollup(example::id_of<example::ScopeId>("zone.1")));
  std::cout << "  rollup zone.1 covers " << zone_rollup.totals.scope_count
            << " scopes, allocatable "
            << zone_rollup.totals.totals.allocatable.milliwatts() << "mW\n";

  EXAMPLE_ASSIGN(class_rollup,
                 ledger.class_rollup(example::id_of<example::EquipmentClassId>(
                     "class.crah")));
  std::cout << "  rollup class.crah declared "
            << class_rollup.totals.declared_installed.milliwatts() << "mW\n";

  EXAMPLE_ASSIGN(medium_rollup, ledger.medium_rollup(example::Medium::Air));
  std::cout << "  rollup medium Air declared "
            << medium_rollup.totals.declared_installed.milliwatts() << "mW\n";

  EXAMPLE_REQUIRE(site_rollup.totals.declared_installed.milliwatts() == 1'000'000,
                  "the site rollup must sum both zones");
  EXAMPLE_REQUIRE(class_rollup.totals.declared_installed.milliwatts() ==
                      site_rollup.totals.declared_installed.milliwatts(),
                  "the class rollup must cover the same total");
  std::cout << "  OK\n";
  return 0;
}
