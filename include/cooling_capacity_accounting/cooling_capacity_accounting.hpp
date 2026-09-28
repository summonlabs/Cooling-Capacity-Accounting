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

#ifndef COOLING_CAPACITY_ACCOUNTING_COOLING_CAPACITY_ACCOUNTING_HPP
#define COOLING_CAPACITY_ACCOUNTING_COOLING_CAPACITY_ACCOUNTING_HPP

/// Umbrella header for the Cooling Capacity Accounting public interface.
///
/// This library owns the constituent, provenance-bound accounting of cooling
/// capacity: installed, allocatable, withheld, degraded, unavailable,
/// indeterminate and residual thermal-removal quantities by zone, loop and
/// equipment class, with mechanically checked closure. It does not decide
/// placement, admission, reservation, actuation, failover or emergency policy.

#include "cooling_capacity_accounting/accounting.hpp"
#include "cooling_capacity_accounting/canonical.hpp"
#include "cooling_capacity_accounting/clock.hpp"
#include "cooling_capacity_accounting/contribution.hpp"
#include "cooling_capacity_accounting/digest.hpp"
#include "cooling_capacity_accounting/domain.hpp"
#include "cooling_capacity_accounting/engine.hpp"
#include "cooling_capacity_accounting/errors.hpp"
#include "cooling_capacity_accounting/evidence.hpp"
#include "cooling_capacity_accounting/ids.hpp"
#include "cooling_capacity_accounting/interchange.hpp"
#include "cooling_capacity_accounting/ledger.hpp"
#include "cooling_capacity_accounting/limits.hpp"
#include "cooling_capacity_accounting/measure.hpp"
#include "cooling_capacity_accounting/policy.hpp"
#include "cooling_capacity_accounting/scope.hpp"
#include "cooling_capacity_accounting/snapshot.hpp"
#include "cooling_capacity_accounting/store.hpp"
#include "cooling_capacity_accounting/text.hpp"
#include "cooling_capacity_accounting/units.hpp"
#include "cooling_capacity_accounting/version.hpp"
#include "cooling_capacity_accounting/wide.hpp"

#endif  // COOLING_CAPACITY_ACCOUNTING_COOLING_CAPACITY_ACCOUNTING_HPP
