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

#ifndef COOLING_CAPACITY_ACCOUNTING_SRC_FILE_LOCK_HPP
#define COOLING_CAPACITY_ACCOUNTING_SRC_FILE_LOCK_HPP

// Internal cross-process file lock. The store holds one of these for its whole
// lifetime so that two processes can never believe they are the writer.

#include <filesystem>

#include "cooling_capacity_accounting/errors.hpp"

namespace cooling_capacity_accounting {
namespace internal {

class FileLock {
 public:
  FileLock() = default;
  ~FileLock();

  FileLock(const FileLock&) = delete;
  FileLock& operator=(const FileLock&) = delete;
  FileLock(FileLock&& other) noexcept;
  FileLock& operator=(FileLock&& other) noexcept;

  /// Acquires an exclusive or shared lock on path. When non_blocking is true a
  /// lock already held elsewhere yields StoreInUse instead of waiting.
  [[nodiscard]] static Result<FileLock> acquire(const std::filesystem::path& path,
                                                bool exclusive, bool non_blocking);

  [[nodiscard]] Result<void> release();
  [[nodiscard]] bool held() const noexcept;

 private:
#ifdef _WIN32
  void* handle_ = nullptr;
#else
  int descriptor_ = -1;
#endif
};

}  // namespace internal
}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_SRC_FILE_LOCK_HPP
