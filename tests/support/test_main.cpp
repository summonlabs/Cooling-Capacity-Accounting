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

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"
#include "fixtures.hpp"
#include "test_harness.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using namespace cooling_capacity_accounting;
using cca_test::fixture_now;
using cca_test::read_text_file;
using cca_test::report_field;
using cca_test::write_text_file;

void report(const std::string& path, const std::string& text) {
  if (!path.empty()) {
    write_text_file(std::filesystem::path(path), text);
  }
}

[[nodiscard]] std::string describe(const Error& error) {
  return std::string(error_code_name(error.code())) + " " + error.to_string();
}

[[nodiscard]] std::int64_t parse_argument(const std::string& text) {
  return std::strtoll(text.c_str(), nullptr, 10);
}

/// Publishes one synthetic generation through the public engine path.
Result<AccountingSnapshot> publish_synthetic(AccountingEngine& engine,
                                             std::size_t contributions,
                                             AccountGeneration expected_previous,
                                             Timestamp published_at,
                                             std::uint64_t seed) {
  cca_test::FacilitySpec spec;
  spec.zones = 2;
  spec.loops_per_zone = 2;
  spec.units_per_loop = (contributions + 3U) / 4U;
  AccountingInput input = cca_test::make_facility(spec);
  const Result<StateRevision> revision = StateRevision::of(expected_previous.value());
  if (!revision.ok()) {
    return revision.error();
  }
  input.generations.revision = revision.value();
  input.policy.generation = input.generations.policy;
  const std::string material = "attempt-" + std::to_string(seed) + "-" +
                               std::to_string(expected_previous.value());
  const Result<AttemptId> attempt = AttemptId::from_material(material);
  if (!attempt.ok()) {
    return attempt.error();
  }
  return engine.publish(std::move(input), fixture_now(), published_at, attempt.value());
}

int run_child(const std::vector<std::string>& arguments) {
  if (arguments.size() < 3U) {
    return 100;
  }
  const std::string& mode = arguments[1];
  const std::filesystem::path root(arguments[2]);

  if (mode == "hold-writer") {
    const std::string ready = arguments.size() > 3U ? arguments[3] : std::string();
    const std::int64_t hold_ms = arguments.size() > 4U ? parse_argument(arguments[4]) : 500;
    EngineOptions options;
    options.root = root;
    Result<AccountingEngine> engine = AccountingEngine::open(options);
    if (!engine.ok()) {
      report(ready + ".error", describe(engine.error()));
      return 2;
    }
    report(ready, "ready");
    std::this_thread::sleep_for(std::chrono::milliseconds(hold_ms));
    const Result<void> closed = engine.value().close();
    return closed.ok() ? 0 : 3;
  }

  if (mode == "open-readonly" || mode == "verify") {
    const std::string out = arguments.size() > 3U ? arguments[3] : std::string();
    StoreOptions options;
    options.root = root;
    Result<AccountingStore> store = AccountingStore::open_read_only(options);
    if (!store.ok()) {
      report(out, "status=error code=" +
                      std::string(error_code_name(store.error().code())));
      return 0;
    }
    if (mode == "open-readonly") {
      report(out, "status=ok generations=" +
                      std::to_string(store.value().current_generation().value()));
      return 0;
    }
    Result<AccountingSnapshot> snapshot = store.value().latest();
    if (!snapshot.ok()) {
      report(out, "status=error code=" +
                      std::string(error_code_name(snapshot.error().code())));
      return 0;
    }
    const Result<void> verified = snapshot.value().verify();
    if (!verified.ok()) {
      report(out, "status=verify-failed code=" +
                      std::string(error_code_name(verified.error().code())));
      return 0;
    }
    const ClosureCheck closure = snapshot.value().ledger().verify_closure();
    report(out, "status=ok generation=" +
                    std::to_string(snapshot.value().header().generation.value()) +
                    " digest=" + snapshot.value().content_digest().to_hex() +
                    " closure=" + (closure.exact ? "exact" : "violated") +
                    " recovered=" +
                    (snapshot.value().header().recovered ? "true" : "false"));
    return 0;
  }

  if (mode == "publish" || mode == "publish-loop") {
    const std::size_t count = static_cast<std::size_t>(parse_argument(arguments[3]));
    const std::size_t contributions =
        static_cast<std::size_t>(parse_argument(arguments[4]));
    const std::string out = arguments.size() > 5U ? arguments[5] : std::string();
    EngineOptions options;
    options.root = root;
    Result<AccountingEngine> engine = AccountingEngine::open(options);
    if (!engine.ok()) {
      report(out, "status=error code=" +
                      std::string(error_code_name(engine.error().code())));
      return 2;
    }
    for (std::size_t index = 0; index < count; ++index) {
      const Timestamp published = Timestamp::of_unix_milliseconds(
          fixture_now().unix_milliseconds() + static_cast<std::int64_t>(index));
      Result<AccountingSnapshot> snapshot = publish_synthetic(
          engine.value(), contributions, engine.value().status().value().generation,
          published, index);
      if (!snapshot.ok()) {
        report(out, "status=error code=" +
                        std::string(error_code_name(snapshot.error().code())));
        return 3;
      }
      report(out, "status=ok published=" + std::to_string(index + 1) + " generation=" +
                      std::to_string(snapshot.value().header().generation.value()));
    }
    const Result<void> closed = engine.value().close();
    return closed.ok() ? 0 : 4;
  }

  if (mode == "publish-epoch") {
    const std::uint64_t epoch = static_cast<std::uint64_t>(parse_argument(arguments[3]));
    const std::string out = arguments.size() > 4U ? arguments[4] : std::string();
    EngineOptions options;
    options.root = root;
    const Result<ControlPlaneEpoch> epoch_value = ControlPlaneEpoch::of(epoch);
    if (!epoch_value.ok()) {
      report(out, "status=error code=OutOfRange");
      return 2;
    }
    options.epoch = epoch_value.value();
    Result<AccountingEngine> engine = AccountingEngine::open(options);
    if (!engine.ok()) {
      report(out, "status=error code=" +
                      std::string(error_code_name(engine.error().code())));
      return 0;
    }
    Result<AccountingSnapshot> snapshot = publish_synthetic(
        engine.value(), 4U, engine.value().status().value().generation, fixture_now(), 1U);
    if (!snapshot.ok()) {
      report(out, "status=error code=" +
                      std::string(error_code_name(snapshot.error().code())));
      return 0;
    }
    report(out, "status=ok generation=" +
                    std::to_string(snapshot.value().header().generation.value()));
    return 0;
  }

  if (mode == "publish-forever") {
    const std::size_t contributions =
        static_cast<std::size_t>(parse_argument(arguments[3]));
    EngineOptions options;
    options.root = root;
    Result<AccountingEngine> engine = AccountingEngine::open(options);
    if (!engine.ok()) {
      return 2;
    }
    for (std::size_t index = 0;; ++index) {
      const Timestamp published = Timestamp::of_unix_milliseconds(
          fixture_now().unix_milliseconds() + static_cast<std::int64_t>(index));
      const Result<AccountingSnapshot> snapshot =
          publish_synthetic(engine.value(), contributions,
                            engine.value().status().value().generation, published, index);
      if (!snapshot.ok()) {
        return 5;
      }
    }
  }

  if (mode == "reopen-check") {
    const std::string out = arguments.size() > 3U ? arguments[3] : std::string();
    EngineOptions options;
    options.root = root;
    Result<AccountingEngine> engine = AccountingEngine::open(options);
    if (!engine.ok()) {
      report(out, "status=error code=" +
                      std::string(error_code_name(engine.error().code())));
      return 0;
    }
    const Result<EngineStatus> status = engine.value().status();
    if (!status.ok()) {
      report(out, "status=error code=StatusUnavailable");
      return 0;
    }
    report(out, "status=ok generation=" + std::to_string(status.value().generation.value()) +
                    " recovery=" +
                    (status.value().recovered_pending_revalidation ? "pending"
                                                                   : "fresh"));
    return 0;
  }

  return 101;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  // Deterministic, non-interactive failure behaviour: no Windows Error
  // Reporting dialog and no debugger chooser may ever appear, including when a
  // crash test terminates a child process on purpose.
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
  std::vector<std::string> arguments;
  for (int index = 1; index < argc; ++index) {
    arguments.emplace_back(argv[index]);
  }
  if (!arguments.empty() && arguments[0] == "--child") {
    return run_child(arguments);
  }
  std::string filter;
  bool list_only = false;
  for (const std::string& argument : arguments) {
    if (argument.rfind("--filter=", 0) == 0) {
      filter = argument.substr(9);
    } else if (argument == "--list") {
      list_only = true;
    }
  }
  return cca_test::run_all(filter, list_only);
}
