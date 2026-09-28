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

#ifndef COOLING_CAPACITY_ACCOUNTING_TESTS_REFERENCE_MODEL_HPP
#define COOLING_CAPACITY_ACCOUNTING_TESTS_REFERENCE_MODEL_HPP

// An independent reference model. It is written from the documented semantics
// rather than from the library implementation: it enumerates every contribution
// one at a time and derives the disposition of each from first principles, so
// it can disagree with the ledger instead of agreeing with it by construction.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

namespace cca_test {

struct ReferenceTotals {
  std::int64_t declared_installed = 0;
  std::int64_t allocatable = 0;
  std::int64_t withheld = 0;
  std::int64_t degraded_loss = 0;
  std::int64_t unavailable = 0;
  std::int64_t indeterminate = 0;
};

/// Reference accounting of the additive subset of the model: contributions
/// whose class is Additive, whose sharing is Exclusive, whose installed
/// quantity is known and whose service state is InService or OutOfService.
/// The model refuses to answer for any input outside that subset.
class ReferenceModel {
 public:
  static cooling_capacity_accounting::Result<ReferenceModel> build(
      const cooling_capacity_accounting::AccountingInput& input);

  [[nodiscard]] const ReferenceTotals& scope(const std::string& scope) const;
  [[nodiscard]] const std::map<std::string, ReferenceTotals>& scopes() const noexcept {
    return scopes_;
  }
  /// Sum of the per-scope cells, which must reconstruct the overall totals.
  [[nodiscard]] ReferenceTotals overall() const;

 private:
  std::map<std::string, ReferenceTotals> scopes_;
};

}  // namespace cca_test

#endif  // COOLING_CAPACITY_ACCOUNTING_TESTS_REFERENCE_MODEL_HPP
