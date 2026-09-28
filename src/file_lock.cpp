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

#include "file_lock.hpp"

#include <filesystem>
#include <string>
#include <utility>

#include "cooling_capacity_accounting/text.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace cooling_capacity_accounting {
namespace internal {
namespace {

#ifdef _WIN32

[[nodiscard]] std::string system_message(unsigned long code) {
  return "windows error " + to_decimal(static_cast<std::uint64_t>(code));
}

#else

[[nodiscard]] std::string system_message(int code) {
  return "errno " + to_decimal(static_cast<std::uint64_t>(code));
}

#endif

}  // namespace

FileLock::~FileLock() {
  const Result<void> released = release();
  static_cast<void>(released);
}

FileLock::FileLock(FileLock&& other) noexcept {
#ifdef _WIN32
  handle_ = other.handle_;
  other.handle_ = nullptr;
#else
  descriptor_ = other.descriptor_;
  other.descriptor_ = -1;
#endif
}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
  if (this != &other) {
    const Result<void> released = release();
    static_cast<void>(released);
#ifdef _WIN32
    handle_ = other.handle_;
    other.handle_ = nullptr;
#else
    descriptor_ = other.descriptor_;
    other.descriptor_ = -1;
#endif
  }
  return *this;
}

bool FileLock::held() const noexcept {
#ifdef _WIN32
  return handle_ != nullptr;
#else
  return descriptor_ >= 0;
#endif
}

Result<FileLock> FileLock::acquire(const std::filesystem::path& path, bool exclusive,
                                   bool non_blocking) {
  FileLock lock;
#ifdef _WIN32
  const std::wstring wide = path.wstring();
  HANDLE handle = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    const unsigned long code = GetLastError();
    if (code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION) {
      return Error::of(ErrorCode::StoreInUse,
                       "another process holds the store's writer lock")
          .with_detail(system_message(code));
    }
    return Error::of(ErrorCode::LockFailure, "the lock file could not be opened")
        .with_subject(path.string())
        .with_detail(system_message(code));
  }
  // Give the file one byte so the locked range is inside the file.
  const char marker = 'C';
  DWORD written = 0;
  static_cast<void>(WriteFile(handle, &marker, 1, &written, nullptr));
  static_cast<void>(FlushFileBuffers(handle));

  OVERLAPPED overlapped = {};
  DWORD flags = LOCKFILE_FAIL_IMMEDIATELY;
  if (exclusive) {
    flags |= LOCKFILE_EXCLUSIVE_LOCK;
  }
  if (!LockFileEx(handle, flags, 0, 1, 0, &overlapped)) {
    const unsigned long code = GetLastError();
    CloseHandle(handle);
    if (code == ERROR_LOCK_VIOLATION || code == ERROR_IO_PENDING) {
      return Error::of(ErrorCode::StoreInUse,
                       "another process holds the store's writer lock")
          .with_detail(system_message(code));
    }
    return Error::of(ErrorCode::LockFailure, "the store lock could not be acquired")
        .with_subject(path.string())
        .with_detail(system_message(code));
  }
  static_cast<void>(non_blocking);
  lock.handle_ = handle;
  return lock;
#else
  const int descriptor = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
  if (descriptor < 0) {
    const int code = errno;
    if (code == EACCES || code == EROFS || code == EPERM) {
      return Error::of(ErrorCode::PermissionDenied,
                       "the lock file could not be opened")
          .with_subject(path.string())
          .with_detail(system_message(code));
    }
    return Error::of(ErrorCode::LockFailure, "the lock file could not be opened")
        .with_subject(path.string())
        .with_detail(system_message(code));
  }
  int operation = exclusive ? LOCK_EX : LOCK_SH;
  if (non_blocking) {
    operation |= LOCK_NB;
  }
  if (::flock(descriptor, operation) != 0) {
    const int code = errno;
    ::close(descriptor);
    if (code == EWOULDBLOCK || code == EAGAIN) {
      return Error::of(ErrorCode::StoreInUse,
                       "another process holds the store's writer lock")
          .with_detail(system_message(code));
    }
    return Error::of(ErrorCode::LockFailure, "the store lock could not be acquired")
        .with_subject(path.string())
        .with_detail(system_message(code));
  }
  lock.descriptor_ = descriptor;
  return lock;
#endif
}

Result<void> FileLock::release() {
#ifdef _WIN32
  if (handle_ == nullptr) {
    return Ok{};
  }
  HANDLE handle = static_cast<HANDLE>(handle_);
  OVERLAPPED overlapped = {};
  static_cast<void>(UnlockFileEx(handle, 0, 1, 0, &overlapped));
  static_cast<void>(CloseHandle(handle));
  handle_ = nullptr;
  return Ok{};
#else
  if (descriptor_ < 0) {
    return Ok{};
  }
  static_cast<void>(::flock(descriptor_, LOCK_UN));
  static_cast<void>(::close(descriptor_));
  descriptor_ = -1;
  return Ok{};
#endif
}

}  // namespace internal
}  // namespace cooling_capacity_accounting
