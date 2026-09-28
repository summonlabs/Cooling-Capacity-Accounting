# Contributing to Cooling Capacity Accounting

Thank you for your interest in contributing to Cooling Capacity Accounting.
This document describes the contribution terms and the engineering expectations
for this repository.

## License

By contributing to this project, you agree that your contributions are licensed
under the **Apache License, Version 2.0**. See the `LICENSE` file for the full
license text and the `NOTICE` file for attribution and license notices. There is
**no separate Contributor License Agreement (CLA)** requirement: you retain
ownership of your contributions and grant the project a license to use them
under the terms of the Apache License 2.0.

## License headers

New source files should carry the following header:

```
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
```

## Coding standards

- C++20 and CMake only. No third-party dependencies, no network access during
  configure, build or test.
- Build cleanly with `/W4 /WX` on MSVC and `-Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion -Werror` elsewhere. Fix warning causes rather
  than suppressing warnings.
- Public identities, generations, epochs, revisions, attempts, observations and
  commit sequences are distinct strong types. Do not add implicit conversions
  between them.
- Accounting is exact integer arithmetic in fixed canonical units. Floating
  point must not appear in any authoritative quantity, comparison or boundary.
- Every quantity carries an explicit unit in its type. Use the checked
  arithmetic helpers; never let an unchecked operation reach an authority
  boundary.
- All external input is untrusted. Validate before allocating, bound every
  user-controlled size, and reject malformed input instead of normalizing it.
- Zero, unknown, unavailable, unsupported, stale and absent are different
  answers. Do not collapse them, and do not add a policy option that would.
- Where iteration order is public or serialized it is documented, total and
  tested. Do not rely on hash-container iteration order for anything observable.
- The public API reports failures as `Result<T>` values with stable
  `ErrorCode`s. Do not use exceptions as the machine-readable contract.

## Architecture boundaries

- This repository owns **cooling-capacity accounting**: the constituent,
  provenance-bound accounting of installed, allocatable, withheld, degraded,
  unavailable, indeterminate and residual thermal-removal quantities, by zone,
  loop and equipment class, with mechanically checked closure.
- This repository does **not** own facility-level allocatable capacity
  composition or admission (Tranche 2 Cooling Capacity), placement, capacity
  reservation, cooling actuation, cooling failover, thermal emergency policy,
  thermal topology, or ASI/DFI scheduling. Do not add code that decides any of
  those questions.
- External state owned by other systems arrives as typed, generation-stamped
  evidence or as an opaque reference. Do not reimplement a BMS, DCIM, PLC or
  plant controller here.
- Never derive availability from topology alone, and never infer equipment
  independence from the fact that two devices have different identifiers.

## Build and test

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests are proof obligations. A change that adds accounting behavior should add
the property, boundary or adversarial test that would catch its regression, and
should keep the independent reference model in `tests/support` in agreement
with the library.

## Commits

Keep commits focused and their messages public-facing, concise and neutral.
Do not add co-author trailers or automated-attribution trailers of any kind.
