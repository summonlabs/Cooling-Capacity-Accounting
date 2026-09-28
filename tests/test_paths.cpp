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

// Store root paths: what is refused, what is repaired, and the rule that two
// spellings of one directory can never obtain two writer locks.

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "fixtures.hpp"
#include "test_harness.hpp"

using namespace cooling_capacity_accounting;
using cca_test::TempDir;
using cca_test::describe_error;
using cca_test::note;
using cca_test::write_text_file;

namespace {

[[nodiscard]] std::string describe(const std::filesystem::path& root) {
  return root.generic_string();
}

/// Opens the root for writing, requires success and closes it again.
void expect_open_ok(const std::filesystem::path& root, const char* what) {
  StoreOptions options;
  options.root = root;
  Result<AccountingStore> store = AccountingStore::open(options);
  if (!store.ok()) {
    CCA_FAIL(std::string(what) + " should open but failed: " +
             describe_error(store.error()) + " (" + describe(root) + ")");
    return;
  }
  CCA_CHECK(store.value().is_open());
  CCA_REQUIRE_OK(store.value().close());
}

void expect_refused(ErrorCode expected, const std::filesystem::path& root,
                    const char* what) {
  const Result<void> validated = AccountingStore::validate_root(root);
  if (validated.ok() || validated.error().code() != expected) {
    CCA_FAIL(std::string(what) + ": expected " + std::string(error_code_name(expected)) +
             " from validate_root but got " +
             (validated.ok() ? std::string("success") : describe_error(validated.error())) +
             " (" + describe(root) + ")");
  }
  StoreOptions options;
  options.root = root;
  const Result<AccountingStore> opened = AccountingStore::open(options);
  if (opened.ok() || opened.error().code() != expected) {
    CCA_FAIL(std::string(what) + ": expected " + std::string(error_code_name(expected)) +
             " from open but got " +
             (opened.ok() ? std::string("success") : describe_error(opened.error())) + " (" +
             describe(root) + ")");
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

CCA_TEST(paths_refuses_an_empty_root) {
  const std::filesystem::path empty;
  CCA_CHECK(empty.empty());
  expect_refused(ErrorCode::PathInvalid, empty, "an empty root");

  // The refusal happens before the filesystem is touched, so no directory can
  // be created by an empty root.
  StoreOptions options;
  options.root = empty;
  options.create_if_missing = true;
  CCA_CHECK_CODE(AccountingStore::open(options), ErrorCode::PathInvalid);
  CCA_CHECK_CODE(AccountingStore::open_read_only(options), ErrorCode::PathInvalid);
}

CCA_TEST(paths_refuses_a_nul_byte_in_the_root) {
  TempDir dir("paths-nul");
  const std::string with_nul("store\0name", 10);
  const std::filesystem::path embedded = dir.path() / std::filesystem::path(with_nul);
  CCA_CHECK(embedded.generic_string().find('\0') != std::string::npos);
  expect_refused(ErrorCode::PathInvalid, embedded, "a root with an embedded NUL");

  const std::filesystem::path only_nul(std::string("\0", 1));
  CCA_CHECK(!only_nul.empty());
  expect_refused(ErrorCode::PathInvalid, only_nul, "a root that is one NUL byte");

  StoreOptions options;
  options.root = embedded;
  CCA_CHECK_CODE(AccountingStore::open(options), ErrorCode::PathInvalid);
  CCA_CHECK_CODE(AccountingStore::open_read_only(options), ErrorCode::PathInvalid);
}

CCA_TEST(paths_refuses_a_root_longer_than_the_bound) {
  TempDir dir("paths-length");
  const std::string base = dir.path().generic_string();
  CCA_CHECK(base.size() + 2U < kMaxPathLength);

  // Exactly at the bound the path is accepted and the store is created.
  const std::string at_bound_name(kMaxPathLength - base.size() - 1U, 'a');
  const std::filesystem::path at_bound(base + "/" + at_bound_name);
  CCA_CHECK_EQ(at_bound.generic_string().size(), kMaxPathLength);
  CCA_CHECK(AccountingStore::validate_root(at_bound).ok());
  expect_open_ok(at_bound, "a root exactly at the length bound");

  // One character more is refused before anything is touched.
  const std::string over_name(kMaxPathLength - base.size(), 'b');
  const std::filesystem::path over(base + "/" + over_name);
  CCA_CHECK_EQ(over.generic_string().size(), kMaxPathLength + 1U);
  expect_refused(ErrorCode::PathTooLong, over, "a root one character over the bound");

  // The refused path was never created.
  std::error_code code;
  CCA_CHECK(!std::filesystem::exists(over, code));
}

CCA_TEST(paths_refuses_a_traversal_component) {
  TempDir dir("paths-traversal");
  const std::filesystem::path parent_traversal =
      dir.path() / "nested" / ".." / "store";
  expect_refused(ErrorCode::PathTraversalRejected, parent_traversal,
                 "a root with a parent component");

  const std::filesystem::path bare("..");
  expect_refused(ErrorCode::PathTraversalRejected, bare, "a root that is ..");

  const std::filesystem::path dotted("./..");
  expect_refused(ErrorCode::PathTraversalRejected, dotted, "a root that ends in ..");

  StoreOptions options;
  options.root = parent_traversal;
  CCA_CHECK_CODE(AccountingStore::open(options), ErrorCode::PathTraversalRejected);
  CCA_CHECK_CODE(AccountingStore::open_read_only(options), ErrorCode::PathTraversalRejected);
}

CCA_TEST(paths_refuses_a_regular_file_as_the_root) {
  TempDir dir("paths-file");
  const std::filesystem::path file = dir.path() / "not-a-store.txt";
  write_text_file(file, "this is a file, not a store\n");
  CCA_CHECK(std::filesystem::is_regular_file(file));

  StoreOptions options;
  options.root = file;
  options.create_if_missing = true;
  CCA_CHECK_CODE(AccountingStore::open(options), ErrorCode::PathInvalid);

  options.create_if_missing = false;
  CCA_CHECK_CODE(AccountingStore::open(options), ErrorCode::PathInvalid);
  CCA_CHECK(std::filesystem::is_regular_file(file));
  CCA_CHECK_EQ(cca_test::read_text_file(file), std::string("this is a file, not a store\n"));
}

CCA_TEST(paths_refuses_a_missing_root_when_creation_is_disabled) {
  TempDir dir("paths-missing");
  const std::filesystem::path missing = dir.path() / "absent";
  StoreOptions options;
  options.root = missing;
  options.create_if_missing = false;
  CCA_CHECK_CODE(AccountingStore::open(options), ErrorCode::StoreNotFound);
  CCA_CHECK_CODE(AccountingStore::open_read_only(options), ErrorCode::StoreNotFound);

  // Creation is the default, and it creates exactly the root.
  options.create_if_missing = true;
  CCA_ASSIGN(store, AccountingStore::open(options));
  CCA_REQUIRE_OK(store.close());
  CCA_CHECK(std::filesystem::is_directory(missing));
}

// ---------------------------------------------------------------------------
// Repaired spellings
// ---------------------------------------------------------------------------

CCA_TEST(paths_accepts_repaired_spellings_of_one_directory) {
  TempDir dir("paths-spelling");
  const std::string base = dir.path().generic_string();
  const std::filesystem::path native = dir.path() / "store";

  const std::vector<std::filesystem::path> spellings{
      native,
      std::filesystem::path(base + "/store"),
      std::filesystem::path(base + "/./store"),
      std::filesystem::path(base + "//store"),
  };
  for (const std::filesystem::path& spelling : spellings) {
    CCA_CHECK(AccountingStore::validate_root(spelling).ok());
    expect_open_ok(spelling, "a repaired spelling of the store root");
  }
  CCA_CHECK(std::filesystem::is_directory(native));
}

CCA_TEST(paths_two_spellings_cannot_both_hold_the_writer_lock) {
  TempDir dir("paths-two-spellings");
  const std::string base = dir.path().generic_string();
  StoreOptions first_options;
  first_options.root = dir.path() / "store";
  StoreOptions second_options;
  second_options.root = std::filesystem::path(base + "/./store");

  CCA_ASSIGN(first, AccountingStore::open(first_options));
  CCA_CHECK(first.is_open());

  // The second spelling names the same directory, so it cannot obtain a second
  // writer lock.
  CCA_CHECK_CODE(AccountingStore::open(second_options), ErrorCode::StoreInUse);
  CCA_REQUIRE_OK(first.close());

  CCA_ASSIGN(second, AccountingStore::open(second_options));
  CCA_CHECK(second.is_open());
  CCA_REQUIRE_OK(second.close());

  // And the original spelling can be reopened afterwards.
  CCA_ASSIGN(third, AccountingStore::open(first_options));
  CCA_REQUIRE_OK(third.close());
}

// ---------------------------------------------------------------------------
// Reparse points
// ---------------------------------------------------------------------------

CCA_TEST(paths_refuses_a_reparse_point) {
  TempDir dir("paths-reparse");
  const std::filesystem::path real = dir.path() / "real";
  const std::filesystem::path child = real / "child";
  std::error_code code;
  std::filesystem::create_directories(child, code);
  CCA_CHECK(!code);
  // A real store below the target, so the link is not the only thing there.
  expect_open_ok(real, "the real store directory");

  const std::filesystem::path link = dir.path() / "link";
  std::error_code link_code;
  std::filesystem::create_directory_symlink(real, link, link_code);
  if (link_code) {
    note("this environment refused to create a directory symbolic link (" +
         link_code.message() + "); the reparse-point assertions were skipped");
    return;
  }
  CCA_CHECK(std::filesystem::is_symlink(std::filesystem::symlink_status(link)));

  // The link itself is a reparse point.
  expect_refused(ErrorCode::ReparsePointRejected, link, "a symbolic link to a store");
  // A path that traverses the link is refused as well.
  expect_refused(ErrorCode::ReparsePointRejected, link / "child",
                 "a path through a symbolic link");
  // The directory the link points at is not a reparse point and still opens.
  expect_open_ok(real, "the target of the refused link");
  expect_open_ok(child, "a child directory below the refused link target");
}
