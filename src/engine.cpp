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

#include "cooling_capacity_accounting/engine.hpp"

#include <optional>
#include <string>
#include <utility>

#include "cooling_capacity_accounting/digest.hpp"
#include "cooling_capacity_accounting/version.hpp"

namespace cooling_capacity_accounting {
namespace {

[[nodiscard]] Result<Digest> fingerprint_of(const AccountingSnapshot& snapshot) {
  DigestBuilder builder;
  builder.add_section("publish-fingerprint");
  builder.add_field("content", snapshot.content_digest().to_hex());
  builder.add_field("policy", snapshot.header().policy_digest.to_hex());
  builder.add_field_u64("policy_generation",
                        snapshot.header().policy_generation.value());
  builder.add_field_u64("epoch", snapshot.header().generations.epoch.value());
  builder.add_field_u64("topology", snapshot.header().generations.topology.value());
  builder.add_field_u64("evidence", snapshot.header().generations.evidence.value());
  builder.add_field_u64("revision", snapshot.header().generations.revision.value());
  builder.add_field_i64("accounted_at",
                        snapshot.header().accounted_at.unix_milliseconds());
  return builder.finish();
}

}  // namespace

struct AccountingEngine::Impl {
  EngineOptions options;
  AccountingStore store;
  std::optional<AccountingSnapshot> current;
  bool open = false;
  bool read_only = false;
  bool shutting_down = false;
  bool recovered_pending_revalidation = false;
};

AccountingEngine::AccountingEngine() = default;

AccountingEngine::~AccountingEngine() {
  if (impl_ != nullptr && impl_->open) {
    const Result<void> closed = impl_->store.close();
    static_cast<void>(closed);
  }
}

AccountingEngine::AccountingEngine(AccountingEngine&& other) noexcept = default;
AccountingEngine& AccountingEngine::operator=(AccountingEngine&& other) noexcept =
    default;

Result<AccountingEngine> AccountingEngine::open(const EngineOptions& options) {
  if (options.retention_generations == 0) {
    return Error::of(ErrorCode::InvalidArgument,
                     "retention_generations must be at least one");
  }
  CCA_TRY(options.limits.validate());

  StoreOptions store_options;
  store_options.root = options.root;
  store_options.limits = options.limits;
  store_options.create_if_missing = options.create_if_missing;
  store_options.incarnation = options.incarnation;
  store_options.epoch = options.epoch;
  store_options.retention_generations = options.retention_generations;

  CCA_TRY_ASSIGN(store, AccountingStore::open(store_options));

  AccountingEngine engine;
  engine.impl_ = std::make_unique<Impl>();
  engine.impl_->options = options;
  engine.impl_->store = std::move(store);
  engine.impl_->open = true;
  engine.impl_->read_only = false;

  if (engine.impl_->store.has_generation()) {
    CCA_TRY_ASSIGN(snapshot, engine.impl_->store.latest());
    SnapshotHeader header = snapshot.header();
    // Recovered state is not fresh evidence: it stays marked until a later
    // generation revalidates it against current input.
    header.recovered = true;
    AccountingSnapshot marked =
        AccountingSnapshot::from_canonical(snapshot.canonical_bytes(), header,
                                           options.limits)
            .take();
    engine.impl_->current = std::move(marked);
    engine.impl_->recovered_pending_revalidation = true;
  }
  return engine;
}

Result<AccountingSnapshot> AccountingEngine::publish(AccountingInput input,
                                                     Timestamp accounted_at,
                                                     Timestamp published_at,
                                                     AttemptId attempt) {
  if (impl_ == nullptr || !impl_->open) {
    return Error::of(ErrorCode::UnexpectedState, "the engine is not open");
  }
  if (impl_->shutting_down) {
    return Error::of(ErrorCode::ShuttingDown,
                     "the engine is shutting down and publishes no new generation");
  }
  if (impl_->read_only) {
    return Error::of(ErrorCode::NotSupported, "the engine opened the store read only");
  }
  CCA_TRY_ASSIGN(snapshot,
                 AccountingSnapshot::create(std::move(input), accounted_at,
                                            impl_->options.limits));
  CCA_TRY_ASSIGN(fingerprint, fingerprint_of(snapshot));
  PublishRequest request;
  request.attempt = attempt;
  request.fingerprint = fingerprint;
  request.expected_previous = impl_->store.current_generation();
  request.expected_revision = snapshot.header().generations.revision;
  request.epoch = impl_->options.epoch;
  request.published_at = published_at;
  CCA_TRY_ASSIGN(outcome, impl_->store.publish(snapshot, request));

  SnapshotHeader header = snapshot.header();
  header.generation = outcome.record.generation;
  header.commit_sequence = outcome.record.commit_sequence;
  header.published_at = outcome.record.published_at;
  header.writer = outcome.record.epoch == impl_->options.epoch
                      ? impl_->store.fence().incarnation
                      : impl_->store.fence().incarnation;
  header.recovered = false;
  header.recovered_from_generation = 0;
  CCA_TRY_ASSIGN(final_snapshot,
                 AccountingSnapshot::from_canonical(snapshot.canonical_bytes(), header,
                                                    impl_->options.limits));
  impl_->current = final_snapshot;
  impl_->recovered_pending_revalidation = false;
  return final_snapshot;
}

Result<AccountingSnapshot> AccountingEngine::revalidate(AccountingInput fresh,
                                                        Timestamp accounted_at,
                                                        Timestamp published_at,
                                                        AttemptId attempt) {
  if (impl_ == nullptr || !impl_->open) {
    return Error::of(ErrorCode::UnexpectedState, "the engine is not open");
  }
  if (!impl_->recovered_pending_revalidation) {
    return Error::of(ErrorCode::UnexpectedState,
                     "the engine holds no recovered generation to revalidate");
  }
  return publish(std::move(fresh), accounted_at, published_at, attempt);
}

Result<AccountingSnapshot> AccountingEngine::current() const {
  if (impl_ == nullptr || !impl_->open) {
    return Error::of(ErrorCode::UnexpectedState, "the engine is not open");
  }
  if (!impl_->current.has_value()) {
    return Error::of(ErrorCode::NoPublishedGeneration,
                     "the engine has published no accounting generation");
  }
  return impl_->current.value();
}

Result<AccountingSnapshot> AccountingEngine::load(AccountGeneration generation) const {
  if (impl_ == nullptr || !impl_->open) {
    return Error::of(ErrorCode::UnexpectedState, "the engine is not open");
  }
  return impl_->store.load(generation);
}

Result<std::vector<StoredGeneration>> AccountingEngine::history() const {
  if (impl_ == nullptr || !impl_->open) {
    return Error::of(ErrorCode::UnexpectedState, "the engine is not open");
  }
  return impl_->store.history();
}

Result<void> AccountingEngine::begin_shutdown() {
  if (impl_ == nullptr) {
    return Error::of(ErrorCode::UnexpectedState, "the engine is not open");
  }
  impl_->shutting_down = true;
  return Ok{};
}

bool AccountingEngine::shutting_down() const noexcept {
  return impl_ != nullptr && impl_->shutting_down;
}

bool AccountingEngine::recovered_pending_revalidation() const noexcept {
  return impl_ != nullptr && impl_->recovered_pending_revalidation;
}

Result<EngineStatus> AccountingEngine::status() const {
  if (impl_ == nullptr) {
    return Error::of(ErrorCode::UnexpectedState, "the engine is not open");
  }
  EngineStatus status;
  status.open = impl_->open;
  status.read_only = impl_->read_only;
  status.shutting_down = impl_->shutting_down;
  status.recovered_pending_revalidation = impl_->recovered_pending_revalidation;
  status.has_generation = impl_->store.has_generation();
  status.generation = impl_->store.current_generation();
  status.commit_sequence = impl_->store.commit_sequence();
  status.fence = impl_->store.fence();
  Result<std::vector<StoredGeneration>> retained = impl_->store.history();
  status.retained_generations = retained.ok() ? retained.value().size() : 0;
  status.store_root = impl_->options.root.string();
  if (impl_->current.has_value()) {
    status.generation_digest = impl_->current->content_digest();
  }
  return status;
}

Result<void> AccountingEngine::close() {
  if (impl_ == nullptr) {
    return Ok{};
  }
  impl_->shutting_down = true;
  CCA_TRY(impl_->store.close());
  impl_->open = false;
  return Ok{};
}

}  // namespace cooling_capacity_accounting
