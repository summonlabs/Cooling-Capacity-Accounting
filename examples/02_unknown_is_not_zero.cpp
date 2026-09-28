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

// 02 - unknown is not zero
//
// Every facility fact used here is SYNTHETIC.


int main() {
  std::cout << "02 - unknown is not zero (SYNTHETIC facility data)\n";
  example::AccountingInput input = example::make_input();
  input.equipment_classes.push_back(example::make_class(
      "class.crah", example::Medium::Air,
      example::EquipmentClassKind::ComputerRoomAirHandler));
  input.scopes.push_back(example::make_scope("site.alpha", example::ScopeKind::Site, "",
                                             example::Medium::Air));
  input.scopes.push_back(example::make_scope("loop.a", example::ScopeKind::Loop,
                                             "site.alpha", example::Medium::Air));

  input.evidence.push_back(example::make_evidence("evidence.known", 250'000));
  input.contributions.push_back(example::make_unit("unit.known", "loop.a", "class.crah",
                                                   250'000, "evidence.known"));

  // A unit whose nameplate was never established. It is not a zero-capacity
  // unit, and the accounting says so instead of quietly counting nothing.
  example::Contribution unknown_quantity = example::make_unit(
      "unit.unknown-quantity", "loop.a", "class.crah", 0, "evidence.known");
  unknown_quantity.installed = example::Measure<example::ThermalPower>::unknown(
      example::MeasureReason::InventoryUnknown,
      example::BoundedText::from_validated("no nameplate was ever recorded"));
  unknown_quantity.primary_evidence = example::EvidenceId();
  input.contributions.push_back(unknown_quantity);

  // A unit whose nameplate is known but whose service state could not be
  // established. Its quantity is known exactly, so it is accounted as
  // indeterminate mass rather than dropped.
  example::Contribution unknown_state = example::make_unit(
      "unit.unknown-state", "loop.a", "class.crah", 180'000, "evidence.known");
  unknown_state.service = example::ServiceState::Unknown;
  input.contributions.push_back(unknown_state);

  EXAMPLE_ASSIGN(ledger, example::AccountingLedger::build(input, example::now()));
  example::print_scopes(ledger);
  example::print_findings(ledger);
  if (!example::check_closure(ledger)) {
    return 1;
  }

  EXAMPLE_ASSIGN(loop_cell,
                 ledger.scope_rollup(example::id_of<example::ScopeId>("loop.a")));
  const example::ScopeAccounting& loop_scope = loop_cell.scopes.front();
  std::cout << "  loop.a: known mass " << loop_scope.declared_installed.milliwatts()
            << "mW, unknown quantities " << loop_scope.unknown_installed_count
            << ", installed_fully_known="
            << (loop_scope.installed_fully_known ? "true" : "false") << "\n";

  EXAMPLE_REQUIRE(!loop_scope.installed_fully_known,
                  "an unknown nameplate must leave the scope conditional");
  EXAMPLE_REQUIRE(loop_scope.unknown_installed_count == 1,
                  "exactly one contribution has an unknown quantity");
  EXAMPLE_REQUIRE(loop_scope.declared_installed.milliwatts() == 430'000,
                  "the unknown nameplate contributes no mass and is never counted "
                  "as zero-capacity equipment either");
  EXAMPLE_REQUIRE(loop_scope.totals.indeterminate.milliwatts() == 180'000,
                  "the unknown service state leaves its known mass indeterminate");
  EXAMPLE_REQUIRE(loop_scope.status == example::ClosureStatus::Indeterminate,
                  "the scope cannot claim closure while anything is indeterminate");
  std::cout << "  OK\n";
  return 0;
}
