# Cooling Capacity Accounting

Cooling Capacity Accounting is the Data Center Control Plane (DCCP) runtime that
owns the **constituent, provenance-bound accounting of cooling capacity**: which
installed thermal-removal capability exists, how much of it is allocatable,
withheld, degraded, unavailable, indeterminate or unexplained, by zone, loop and
equipment class, and what evidence makes each of those numbers authoritative.

* Portable C++20 library, CMake, no third-party dependencies, no network access.
* Exact integer accounting in canonical fixed units; no floating point takes part
  in any accounting figure, comparison or boundary.
* A mechanically checked accounting identity per scope, with residual and
  conflicting quantities preserved rather than balanced away.
* Immutable, generation-bound snapshots; publication is atomic and durable.
* Durable, integrity-checked, versioned store with cross-process writer fencing
  and explicit recovery.
* Installable and exported as `CoolingCapacityAccounting::cooling_capacity_accounting`.

---

## 1. The question this repository answers

> What cooling-removal capability is authoritatively accounted for across
> thermal zones, loops, equipment classes, redundancy requirements, reserve
> obligations and degraded availability in this exact generation, what remains
> unknown or unavailable, and which residuals or inconsistencies prevent a
> stronger claim?

The answer is an *accounting*, not a decision: numbers, their provenance, the
identity that ties them together, and every reason the answer could be stronger
than it is.

## 2. Systems boundary

### What this repository owns

* Exact cooling-capacity quantities in explicit integer units (milliwatts; a
  ratio is parts per million).
* Installed/nameplate contribution **as evidence**, never as an assumption.
* Currently accounted available (allocatable) contribution.
* Degraded/derated contribution and the exact loss it represents.
* Unavailable and out-of-service contribution.
* Reserve and redundancy obligations as accounted quantities.
* Unknown/unmeasured contribution, counted rather than assumed to be zero.
* Residual and unreconciled quantities, preserved with their sign.
* Zone, loop, equipment-class and medium rollups.
* Provenance and generation of every contributing record.
* Accounting closure and conflict preservation.
* Durable accounting generations, recovery and fencing.

### What this repository explicitly does not own

| Area | Owner |
| --- | --- |
| Facility-level allocatable thermal-removal capacity, admission and placement inputs | **Cooling Capacity** |
| Facility capacity reservations | Facility Capacity Reservation |
| Placement and selection decisions | Facility Placement Planner |
| Cooling actuation, setpoints, valves, pumps | cooling control planes, BMS/DCIM, plant controllers |
| Cooling failover and thermal emergency policy | Cooling Failover, Thermal Emergency Manager |
| Thermal topology structure | Cooling Topology, Thermal Zone Manager |
| Rack/asset inventory, serials, specifications | Rack Registry, Asset Registry |
| Power and space capacity composition | Power Capacity, Space Capacity |
| Accelerator execution, memory, serving, scheduling | ASI |
| Network topology, paths, transport, congestion, federation | DFI |

This repository owns the **constituent accounting surface beneath and alongside**
the Cooling Capacity allocator. It does not reimplement that allocator:
it publishes accounting generations and evidence that a higher layer can consume.

The library reads no sensor, writes no setpoint, opens no network connection and
starts no thread of its own. Every cooling fact it holds arrived as an explicit,
generation-stamped record.

## 3. The accounting model

### 3.1 Units and quantity types

| Quantity | Type | Canonical unit |
| --- | --- | --- |
| Thermal power | `ThermalPower` | milliwatts, range [0, 1e15] (1 TW) |
| Signed difference | `ThermalDelta` | milliwatts, signed |
| Ratio | `Ratio` | parts per million, [0, 1000000] |
| Duration | `DurationMs` | milliseconds, non-negative |
| Instant | `Timestamp` | milliseconds since the Unix epoch, UTC |

Every arithmetic operation that can leave its range returns a `Result` carrying
`NumericOverflow`, `NumericUnderflow`, `OutOfRange` or `DivisionByZero`. The
domain ceiling is a refusal, not a clamp: a quantity above 1 TW is rejected
rather than accumulated.

Exactness is preserved where it matters. `ThermalPower::scaled_by(Ratio)` returns
both the part it apportioned and the remainder it did not, and the two always
re-sum to the whole, so an apportionment that keeps every remainder conserves the
whole exactly. Multiplication by a ratio and composition of up to three derate
factors use a portable 128-bit intermediate (`wide.hpp`) with checked
arithmetic; there is no floating point anywhere in that path.

### 3.2 Contribution classes

Every contribution declares how it combines with its peers. There is no default:
a contribution that does not state its class is refused under the baseline
policy (`ClassificationRequired`).

| Class | Meaning |
| --- | --- |
| `Additive` | Stands on its own. Two additive contributions in one scope both count. |
| `Substitutive` | Members of its group provide the same service; only `required_concurrent` of them may be counted at once, chosen by declared priority then identifier. |
| `Redundant` | Members of its group together satisfy a redundancy class; the derived reserve obligation withholds part of the pool. |
| `ReserveOnly` | Reserve by construction; accounted in full and never allocatable. |
| `MutuallyExclusive` | Exactly one member is counted; the others are withheld with the mutual-exclusion reason. |

A contribution is counted **once**, in the scope its `home_scope` names. Other
scopes may reference it as an alias, which accounts zero and is recorded, so a
shared plant can never be counted twice.

### 3.3 The accounting identity

For one scope:

```
declared_installed == allocatable + withheld + degraded_loss + unavailable + indeterminate
```

Every term is an exact integer and the five categories are disjoint: each
milliwatt of *determinate* accounted mass lands in exactly one of them.
`declared_installed` is the sum of the exactly known installed quantities
accounted in that scope.

* `allocatable` - in service, retained after derates, not withheld.
* `withheld` - in service and retained, but not allocatable: a redundancy
  reserve, a reserve-only contribution, a substitutive spare, a
  mutually-exclusive loser, or a class declared as not contributing.
* `degraded_loss` - the part of an in-service quantity that derates removed.
* `unavailable` - declared out of service.
* `indeterminate` - the quantity is known exactly but its disposition cannot be
  established (unknown service state, stale or missing evidence, an unresolved
  reserve obligation).

Mass whose **quantity** is unknown appears in none of those terms. It is counted
in `unknown_installed_count`, it clears `installed_fully_known`, and the identity
becomes explicitly conditional over the determinate mass. Unknown is never zero.

A disagreement with an external declared total is **not** a sixth category. It is
the signed `residual = external_total - declared_installed`, which may be
positive or negative and is preserved as a finding; folding either sign into an
additive bucket would make the identity meaningless.

`ClosureStatus` summarises one scope, with a fixed precedence:
`Conflicting` > `Indeterminate` > `ClosedWithResidual` > `Closed` > `Empty`.

### 3.4 Derates

A `Degraded` contribution carries one to three derate factors, each with its own
identifier, kind and evidence reference.

* `Absolute` reductions are subtracted first, in sum.
* `Factor` reductions (ppm) are then composed exactly as
  `floor(retained * product(ppm) / 1000000^k)` in the 128-bit domain.

The result therefore does not depend on the order the factors were supplied in,
which is a tested property. A derate with no evidence makes the contribution
indeterminate under the baseline policy: an unevidenced reduction cannot support
an allocatable claim. An absolute reduction larger than the installed quantity
yields zero retention, the whole quantity in `degraded_loss`, and an
`AbsoluteDerateExceedsInstalled` finding.

### 3.5 Redundancy and reserve obligations

A redundancy class states how many independent copies must exist and how many
declared failure domains may be lost at once:

| Class | Copies | Failure domains |
| --- | --- | --- |
| `N` | 1 | 0 |
| `N+1` | 1 | 1 |
| `N+2` | 1 | 2 |
| `2N` | 2 | 0 |
| `2N+1` | 2 | 1 |
| `2N+2` (equivalently `2(N+1)`) | 2 | 2 |

```
required_installed = copies * protected_quantity + (sum of the largest 'failures' domain capacities)
obligation         = required_installed - protected_quantity
withheld           = min(obligation, in-service pool)
```

Domains are ranked by capacity descending, ties broken by domain identifier
ascending, so the result is total and reproducible.

**Independence is never inferred.** Two devices with different identifiers are
not independent. Every in-service member must declare a shared-fate boundary that
the group also lists, that declaration must name evidence, and that evidence must
be current. If any of those is missing, the obligation is unresolved: the group's
**entire in-service pool becomes indeterminate** rather than allocatable, and a
`ReserveUnresolved` finding records why. When the obligation is established,
`required_installed` above the pool is reported as an exact `ReserveShortfall`
rather than absorbed.

### 3.6 Apportioned shared plants

An `Apportioned` contribution splits its installed quantity across declared
shares with floor rounding. Each target scope receives its exact part; the
unapportioned remainder stays in the home scope, where it is reported with an
`UnapportionedShare` finding. Shares may not sum above 1.0
(`OverApportioned`), may not target the home scope (`ShareTargetsHomeScope`),
and may not repeat a target (`DuplicateShareTarget`). Every scope's identity
still holds exactly, because the parts and the remainder re-sum to the whole.

### 3.7 Evidence and freshness

Evidence is a record of something observed elsewhere: a nameplate, a
commissioning report, a capacity test, a BMS or DCIM export, an operator
declaration, an aggregated manifest. It carries its kind, its source, when it was
observed and recorded, the generations it was produced against, and a content
digest. This library never opens the document a reference points at.

`classify_evidence` returns `Current`, `Stale` (older than the policy freshness
window; a zero window never expires), `Superseded` (bound to another epoch,
topology or policy generation, or produced by a later evidence generation than
the accounting one) or `Future` (observed after the accounting instant). Anything
but `Current` makes the contribution indeterminate and is reported with the
corresponding finding. An observation later than the accounting instant is a hard
`FutureTimestamp` error.

When the evidence a contribution cites states a quantity that disagrees with the
accounted one, both are kept: the accounted quantity is unchanged and the signed
difference is preserved as a `NameplateMismatch` finding.

### 3.8 Duplicate identities and conflicts

Records that reuse an identity are handled deterministically and without
fabrication:

* byte-identical contributions are deduplicated, counted once, and reported with
  a `DuplicateContributionIdentity` informational finding;
* contributions with the same identifier and different content are **both
  preserved**: the record with the smaller content digest is counted, the other
  is exposed through `AccountingLedger::conflicting_contributions()`, and a
  `ConflictingContributionIdentity` error finding marks the scope
  `Conflicting`.

The same rules apply to evidence records.

## 4. Authority, generations and fencing

Identity, observation and authority are separate things, and they are separate
types.

* **Identity** is a typed identifier: `SiteId`, `ZoneId`, `LoopId`,
  `EquipmentId`, `EquipmentClassId`, `ScopeId`, `ContributionId`,
  `ContributionGroupId`, `IndependenceDomainId`, `EvidenceId`, `DerateId`,
  `PolicyId`, `ManifestId`. None converts to another.
* **Generations** are distinct counters with distinct meanings and no implicit
  conversion: `ControlPlaneEpoch`, `TopologyGeneration`, `PolicyGeneration`,
  `EvidenceGeneration`, `AccountGeneration`, `StateRevision`,
  `CommitSequence`, `ObservationSequence`.
* **Attempts and incarnations** are 128-bit identifiers. An `AttemptId` is
  derived from request material, so an identical retry is recognisable and a
  different request reusing the identity is detectable.
* **A generation bundle** is the state a request was planned against. Bundles
  from different control-plane epochs compare as `Incomparable`, never as
  "older" or "newer".

Every publication is fenced on the generation the caller believed was current,
the state revision it was planned against, and the control-plane epoch it holds.
A mismatch is refused with a specific code (`PublishPreconditionFailed`,
`StaleEpoch`, `CrossEpochAuthority`, `StateRevisionMismatch`) rather than
applied.

Idempotent retry is defined precisely: the same `AttemptId` with the same
fingerprint replays the recorded outcome, does not advance the commit sequence
and does not re-actuate anything; the same `AttemptId` with a different
fingerprint is `IdempotencyConflict`. Attempt records survive a restart because
they are stored in the manifest.

**Recovered state is not fresh physical evidence.** A generation loaded from the
store is marked `recovered`, and an engine that recovered it reports
`recovered_pending_revalidation` until `revalidate()` publishes a generation
built from current input.

## 5. Persistence and recovery

The store is a directory:

```
LOCK            writer fence, held with an operating-system lock
MANIFEST        the commit point; names the authoritative generation
gen-<20 digits>.cca   one committed generation each
staging-*       in-flight writes; never authoritative
```

A generation file is: an 8-byte magic, a format version, a reserved word that
must be zero, the generation and commit sequence, the epoch, the accounting and
publication instants, the recovered flags, the policy identity and digest, the
generation bundle, the payload length, the canonical payload and a SHA-256 digest
covering everything before it. The manifest carries the writer incarnation, the
epoch, the commit sequence, the current generation, the retained generations with
their digests, the recent attempt fingerprints, and its own digest. Both formats
are bounds-checked before every allocation, reject truncation, trailing bytes,
impossible enumerations and non-zero reserved fields, and are refused rather than
repaired.

**Commit protocol.** Write the staging file; flush it to durable storage; read it
back and verify it byte for byte; rename it into place; build the new manifest;
write, flush and read back the manifest; rename the manifest over the old one.
**That last rename is the commit point.** A crash before it leaves the previous
generation authoritative and a staging file that the next open deletes; a crash
after it leaves exactly the new generation. There is no state in between, which
is what the crash test proves against real process termination.

**Recovery.** The manifest is the only authority. A `gen-*.cca` file the manifest
does not name is never adopted and is removed when the store is opened, because
it was never committed. If the manifest names a generation whose file is missing
or does not verify, the store refuses to open with `IntegrityFailure`; the only
way it moves backwards is the explicit `adopt_previous_generation()`, which
adopts the highest older complete generation, records the rollback in the
manifest, and marks the adopted generation `recovered` with
`recovered_from_generation` set to the abandoned one. With nothing intact to
adopt, the answer is `StoreCorrupt`, not a guess.

**Path hygiene.** A store root may not be empty, contain a NUL byte, exceed the
length bound, contain a `..` component, be a file, or traverse a symbolic link,
junction or other reparse point. The canonical form of the path is what the lock
file and every read and write use, so two spellings of one directory can never
hold two writer locks. Windows path comparison folds case for the same reason.

**Resource bounds.** Record counts, payload sizes, identifier lengths, nesting
depth, derate factors, apportionment targets, aliases, retained generations and
attempt records all have explicit bounds that are checked before the allocation
they bound.

## 6. Concurrency model

The model is deliberately simple and is audited rather than assumed.

* The library starts **no thread**. There is no background work, no work queue
  and therefore no cancellation race.
* Durable mutation has **single-writer authority**: one operating-system lock is
  held for the whole lifetime of an open store. A second process that tries to
  open the same root for writing receives `StoreInUse`; readers share the lock
  and are admitted while no writer holds it.
* An `AccountingLedger` and the snapshot built from it are immutable once
  constructed. Every accessor is a pure read of immutable data, so a published
  snapshot is safe to read from many threads at once, which the concurrency test
  exercises directly.
* The engine is not internally synchronised: it is a single-writer façade, and a
  caller that shares one engine between threads must serialise its mutating
  calls. Its mutating calls are `publish`, `revalidate`, `begin_shutdown` and
  `close`.
* No lock is held across a call into caller code, because there are no callbacks
  in this library at all. There is no lock hierarchy to invert: the store holds
  exactly one lock, and the ledger holds none.
* Shutdown is cooperative and idempotent. Once `begin_shutdown()` has been
  called, `publish` returns `ShuttingDown` and nothing new is committed; queries
  keep answering from the last committed generation; `close()` is safe to call
  twice, and reopening after closing is supported.

## 7. Deterministic semantics

The same invalid request always returns the same primary error. Validation runs
in a documented order, and within a stage in canonical record order:

1. limits and policy validity;
2. policy generation and epoch agreement with the accounting generation;
3. duplicate identities;
4. scope tree structure (parents, kinds, depth, cycles, roots);
5. references (scopes, classes, groups, domains, evidence);
6. group structure;
7. contribution content (class, service, derates, sharing, group membership);
8. observation instants;
9. total quantity ceiling.

No result depends on the order the caller supplied its vectors in: records are
sorted by identifier before anything observable happens, the canonical encoder
produces identical bytes for any permutation of the same records, and the
accounting digest is a function of content alone. Determinism of *semantics* and
determinism of *bytes* are separate claims, and both are tested.

## 8. Architecture

```
include/cooling_capacity_accounting/   the public API, one self-contained header per concept
  errors.hpp       ErrorCode, ErrorCategory, Error, Result<T>, CCA_TRY
  text.hpp         Identifier, BoundedText, DocumentRef, strict UTF-8 and decimals
  wide.hpp         portable 128-bit checked arithmetic
  units.hpp        ThermalPower, ThermalDelta, Ratio, DurationMs, Timestamp
  ids.hpp          typed identities, generations, epochs, revisions, attempts
  digest.hpp       SHA-256 (FIPS 180-4) and the length-prefixed field mixer
  clock.hpp        injected time source; canonical UTC formatting and parsing
  limits.hpp       every bound, checked before the allocation it bounds
  measure.hpp      Known / Unknown / Unsupported, with a machine-readable reason
  domain.hpp       media, equipment classes, scope kinds, redundancy ladder, domains
  evidence.hpp     evidence kinds, sources, values, provenance and freshness
  policy.hpp       evidentiary requirements, tolerances, retention
  contribution.hpp contribution classes, services, derates, sharing, groups
  scope.hpp        scopes and manifest declarations
  ledger.hpp       AccountingInput, AccountingLedger
  accounting.hpp   findings, dispositions, closure status, reserve obligations, rollups
  canonical.hpp    the canonical byte form
  interchange.hpp  the line-oriented interchange text
  snapshot.hpp     AccountingSnapshot and its header
  store.hpp        the durable store
  engine.hpp       the composition of a store and an accounting
src/                            the implementation (not installed)
tools/cca_cli.cpp               inspection and administration CLI
examples/                       eight programs over the stable public API
benchmarks/benchmark_accounting.cpp  completed-operation benchmarks
tests/                          the proof obligations, one file per area
tests/package_consumer/         an independent find_package consumer
```

### Data flow

```
records -> validate -> resolve identities -> account dispositions -> scope cells
                                                                |
                             policy, generations, instant ------+
                                                                v
                                    AccountingLedger (immutable, digest)
                                                                |
        +------------------+-----------------+------------------+
        v                  v                 v                  v
  scope rollup      class/medium       closure check      reserve obligations
                        rollup
        |                  |
        +---------+--------+
                  v
        AccountingSnapshot (canonical bytes + content digest)
                  |
                  v
        AccountingStore (MANIFEST + generations) <- AccountingEngine
```
## 9. Building, testing, installing

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix _install
```

Requirements: CMake 3.20 or newer and a C++20 compiler. MSVC 19.4x is the
primary toolchain (/W4 /WX /permissive-); GCC and Clang are supported with
-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror. There are no
third-party dependencies and configure performs no network access.

Options:

| Option | Default | Effect |
| --- | --- | --- |
| `CCA_BUILD_TESTS` | ON | Build the test suite |
| `CCA_BUILD_TOOLS` | ON | Build the `cca` administration tool |
| `CCA_BUILD_EXAMPLES` | ON | Build the eight example programs |
| `CCA_BUILD_BENCHMARKS` | ON | Build the benchmark |
| `CCA_WARNINGS_AS_ERRORS` | ON | Treat every warning as an error |
| `CCA_SANITIZERS` | OFF | AddressSanitizer (plus UndefinedBehaviorSanitizer on GCC/Clang) |

The library is built as a static library with position-independent code, so it
can be linked into shared objects. Installing exports the namespaced target
`CoolingCapacityAccounting::cooling_capacity_accounting`, the headers, the
`cca` tool, and the package config and version files.

### Consuming the installed package

`tests/package_consumer` is a standalone CMake project that is not part of this
build. It configures only against an installed prefix and links only the exported
target:

```
cmake -S tests/package_consumer -B consumer-build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<install-prefix>
cmake --build consumer-build
consumer-build/cca_package_consumer
```

It performs a real library lifecycle: it accounts a synthetic generation,
publishes it into a store, reopens the store from a fresh object, verifies the
generation, checks the closure identity, and exits non-zero unless every answer
matches.

## 10. Tools, examples and benchmarks

### The `cca` tool

```
cca --version
cca <store-dir> --self-check          # end-to-end invariant and publication check
cca <store-dir> --scenario [--seed N] [--units N]
cca <store-dir> --report [--scope ID] [--class ID] [--medium Air|Liquid]
cca <store-dir> --verify              # re-verify every retained generation
cca <store-dir> --history
cca <store-dir> --export-canonical <file>
cca <store-dir> --export-interchange <file>
cca --check <interchange-file>
cca <store-dir> --ingest <interchange-file> [--accounted-at TS]
```

Exit codes: 0 success, 1 usage, 2 operation failed, 3 verification failed. Every
scenario the tool builds is labelled SYNTHETIC in its own output.

### Examples

| Program | What it demonstrates |
| --- | --- |
| `01_scope_rollup` | A site/zone/loop tree, scope/class/medium rollups, closure |
| `02_unknown_is_not_zero` | Unknown quantity and unknown service state, and why neither is zero |
| `03_derates_and_degradation` | Exact stacked derates and their order independence |
| `04_redundancy_and_reserve` | Declared shared-fate domains, the N+1 reserve, a preserved shortfall, and what an unevidenced declaration does |
| `05_substitution_and_mutual_exclusion` | Substitutive spares and reserve-only contributions |
| `06_apportioned_shared_plant` | An exact split with a preserved unapportioned remainder |
| `07_persistence_and_recovery` | Publication, reopen, the recovered mark, revalidation |
| `08_interchange_and_canonical` | Text round trip, canonical byte form, order independence, refusals |

### Benchmark

```
cca_benchmark --quick     # smoke profile, also run by CTest
cca_benchmark             # full profile
```

Each measurement runs an operation **to completion**: a whole accounting
generation is accounted; a whole site rollup is produced; a whole canonical
encoding is produced and decoded; a whole durable publication has been flushed,
read back and committed by the manifest rename. Submission or enqueue latency is
never measured, because nothing here is queued. The workload uses SYNTHETIC
facility data; the accounting, the encoding, the filesystem work and the process
behaviour are real work performed by the measuring process. Warmup iterations are
run before measurement, and min, median, p95, max and mean are reported
separately.

## 11. Real versus synthetic proof

* **REAL** - everything the test suite observes about a process, a file, a lock,
  a restart or a package: real independent operating-system child processes, real
  process termination, real files on the local filesystem, real corruption of
  those files, real CMake install and find_package consumption, real timing.
* **SYNTHETIC** - every facility, zone, loop, equipment class, contribution,
  evidence record, nameplate value and derate used anywhere in this repository,
  including the benchmark workload. None of it was measured on real cooling
  equipment, and no example or test claims otherwise.
* **UNSUPPORTED** - there is no live BMS, DCIM, chiller plant, CDU, CRAH/CRAC
  installation, pump, valve or PLC in this environment. No hardware validation
  was performed and none is claimed. Vendor firmware, plant controllers and
  building management systems remain external systems that produce evidence this
  library consumes as typed records.

## 12. Validation performed

Host: Windows, MSVC 19.44.35222.0 (Visual Studio 2022 Build Tools), CMake 4.3.2,
Ninja, x64.

| Configuration | Result |
| --- | --- |
| Release (`/W4 /permissive- /WX`) | library, tool, eight examples, benchmark and test suite build with zero warnings |
| Release test suite | 266 tests, 0 failed |
| Release CTest | 12 of 12 passed (test suite, eight examples, two CLI paths, benchmark smoke) |
| Debug (`/RTC1`, `_ITERATOR_DEBUG_LEVEL=2`) | build and CTest 12 of 12 passed |
| AddressSanitizer (MSVC `/fsanitize=address`, `RelWithDebInfo`) | build and CTest 12 of 12 passed, no sanitizer report |
| Install + out-of-tree consumer | `cmake --install` into a private prefix, then a standalone `find_package` project built and executed against that prefix: "consumer OK" |

The suite is run as plain CTest targets, and every test is expected to run to
completion: a test that did not finish would be a defect to diagnose rather than
a case to skip.

Proof kinds inside the suite:

* **Deterministic property and reference-model tests.** Randomised synthetic
  facilities are accounted twice by different routes: once by the ledger and once
  by an independent reference model that enumerates every contribution from first
  principles. Per-scope installed, allocatable, degraded, unavailable and
  indeterminate quantities must agree exactly, and the identity must close.
* **Order independence.** Every input vector, and every nested list inside every
  record, is shuffled repeatedly; the canonical bytes, the accounting digest and
  every per-scope number must be identical.
* **Adversarial input.** Seeded byte mutations of valid canonical payloads,
  truncation at every length, maximum-valued declared lengths and counts, exact
  ceilings and one past them, deep and cyclic scope structures.
* **Corruption of real files.** Truncated generation files and manifests, flipped
  payload and digest bytes, deleted generation files, stale staging files and
  uncommitted generation files - each reopened and asserted against the exact
  expected error or recovery.
* **Real process death.** A child process is started, publishes in a loop, and is
  terminated by the operating system; after each kill the store must resolve to
  exactly one whole generation and accept a new publication.
* **Real multi-process authority.** Independent processes contend for the writer
  lock, read the committed generation, and are refused when they hold a different
  control-plane epoch.
* **Real restarts.** Generations published by a child process are recovered by the
  parent, marked as pending revalidation, and revalidated.
* **Concurrency.** Many threads read one immutable snapshot while other threads
  build independent ledgers; repeated open/close cycles; several read-only stores
  at once.

Benchmark (full profile, 3 warmup iterations then 20 measured iterations per
workload, one operation run to completion per iteration, SYNTHETIC facility data,
REAL accounting and REAL durable storage):

| Workload | 64 contributions | 512 | 2048 |
| --- | --- | --- | --- |
| Account one generation (median) | 539 us | 2348 us | 7731 us |
| One site rollup (median) | 43 us | 79 us | 190 us |
| Canonical encode (median) | 78 us | 422 us | 1510 us |
| Canonical decode (median) | 179 us | 828 us | 3166 us |
| Durable publication, flush + read-back + commit (median) | 20434 us | 25634 us | 52257 us |

The durable figure is dominated by the cost of flushing the staged generation and
the manifest to stable storage, which is the point of measuring a completed
publication rather than an enqueue.

## 13. Status and limitations

* The accounting covers cooling thermal-removal capacity only. Power, space and
  facility-wide composition are other runtimes' questions and are not answered
  here.
* The redundancy ladder is expressed in installed capacity and declared failure
  domains. It does not model hydraulic or airflow coupling between loops: that
  belongs to the cooling topology and control planes, and their conclusions must
  arrive here as evidence.
* The canonical payload bound, the record bounds and the retention bound are
  compile-time limits; a caller may only lower them through `Limits`.
* The store is a local directory. Multi-site federation, replication and
  distributed consensus are out of scope by design.
* The library is a static library; a shared-library build with a stable ABI is
  not offered.
* Recovered generations are marked as such and remain marked until a later
  generation revalidates them. This is deliberate: a recovered number is not a
  fresh measurement.
* No live cooling hardware was exercised. There is no chiller plant, CDU,
  CRAH/CRAC unit, pump, valve, BMS or DCIM in the development environment, so no
  hardware validation is claimed anywhere in this repository. Every facility fact
  used by the examples, the benchmark and the tests is synthetic, and the adapter
  boundary is an evidence record rather than a device driver.
* The interchange text format is ASCII and line oriented. It is complete for the
  records this library accepts, but it is not a general-purpose serialization
  format and does not carry binary evidence payloads - only references to them.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
