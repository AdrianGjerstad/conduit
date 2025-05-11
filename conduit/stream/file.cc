// Copyright 2025 Adrian Gjerstad
//
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
//
// -----------------------------------------------------------------------------
// File: file.cc
// -----------------------------------------------------------------------------
//
// This file defines Read- and Write- FileStream classes and how they use
// Conduit to enable non-blocking and asynchronous operations.
//

#include "conduit/stream/file.h"

#include <errno.h>
#include <fcntl.h>

#include <filesystem>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

#include "conduit/conduit.h"
#include "conduit/event.h"
#include "conduit/stream/stream.h"

namespace cd {

absl::StatusOr<std::shared_ptr<ReadFileStream>> ReadFileStream::Open(
  Conduit* conduit, const std::filesystem::path& path) {
  // Obtain a file descriptor
  int fd = open(path.string().c_str(), O_RDONLY | O_NONBLOCK);

  if (fd < 0) {
    return absl::ErrnoToStatus(
      errno,
      absl::StrFormat("failed to open file '%s' for reading", path.string())
    );
  }

  return std::make_shared<ReadFileStream>(conduit, fd);
}

ReadFileStream::ReadFileStream(Conduit* conduit, int fd) : conduit_(conduit),
  fd_(fd) {
  listener_ = std::make_shared<EventListener>(fd);

  // Schedule some reads to start happening.
  conduit_->OnNext([this]() {
    DoRead();
  });

  listener_->OnHangup([this](int fd) {
    // Shouldn't happen with a file but oh well
    HandleHup();
  });

  // fd was just created, it can't already be registered
  conduit->Add(listener_).IgnoreError();
}

void ReadFileStream::CloseResource() {
  // Don't care if fd is not already registered. We just want to make sure that
  // it's not.
  conduit_->Remove(listener_).IgnoreError();
  close(fd_);
}

void ReadFileStream::DoRead() {
  if (listener_->HasReadable()) {
    listener_->OffReadable();
  }

  std::string buffer(4096, 0);
  ssize_t count = read(fd_, buffer.data(), buffer.size());

  if (count < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // We need to assign a listener
      listener_->OnReadable([this](int fd) {
        DoRead();
      });
    }

    return;
  }

  if (count == 0) {
    // EOF
    Close();
  }

  absl::string_view view(buffer);
  view.remove_suffix(buffer.size() - count);
  HandleData(view);

  // Schedule the next read.
  conduit_->OnNext([this]() {
    DoRead();
  });
}

absl::StatusOr<std::shared_ptr<WriteFileStream>> WriteFileStream::OpenTrunc(
  Conduit* conduit, const std::filesystem::path& path, int mode) {
  // Obtain a file descriptor
  int fd = open(path.string().c_str(), O_CREAT | O_WRONLY | O_TRUNC |
                                       O_NONBLOCK, mode);

  if (fd < 0) {
    return absl::ErrnoToStatus(
      errno,
      absl::StrFormat("failed to open file '%s' for writing", path.string())
    );
  }

  return std::make_shared<WriteFileStream>(conduit, fd);
}

absl::StatusOr<std::shared_ptr<WriteFileStream>> WriteFileStream::OpenAppend(
  Conduit* conduit, const std::filesystem::path& path, int mode) {
  // Obtain a file descriptor
  int fd = open(path.string().c_str(), O_CREAT | O_WRONLY | O_APPEND |
                                       O_NONBLOCK, mode);

  if (fd < 0) {
    return absl::ErrnoToStatus(
      errno,
      absl::StrFormat("failed to open file '%s' for writing", path.string())
    );
  }

  return std::make_shared<WriteFileStream>(conduit, fd);
}

WriteFileStream::WriteFileStream(Conduit* conduit, int fd) : conduit_(conduit),
  fd_(fd) {
  listener_ = std::make_shared<::cd::EventListener>(fd);

  listener_->OnHangup([this](int fd) {
    // Should really only happen if we try writing to a console or something
    // when it disconnects.
    HandleHup();
  });

  conduit_->Add(listener_).IgnoreError();
}

size_t WriteFileStream::TryWriteData(absl::string_view data) {
  ssize_t count = write(fd_, data.data(), data.size());

  if (count < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // We must set an OnWritable listener. The kernel buffer must be full.
      listener_->OnWritable([this](int fd) {
        TryFlush();
      });
    }

    return 0;
  }

  if ((size_t)count < data.size()) {
    // Not everything was written. The kernel buffer must be full.
    listener_->OnWritable([this](int fd) {
      TryFlush();
    });
    return count;
  }

  // Everything was written.
  if (listener_->HasWritable()) {
    listener_->OffWritable();
  }

  return count;
}

void WriteFileStream::CloseResource() {
  conduit_->Remove(listener_).IgnoreError();
  close(fd_);
}

}

