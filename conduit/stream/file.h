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
// File: file.h
// -----------------------------------------------------------------------------
//
// This file declares read- and write- file streams for use in disk I/O
// intensive applications.
//

#ifndef CONDUIT_STREAM_FILE_H_
#define CONDUIT_STREAM_FILE_H_

#include <filesystem>
#include <memory>

#include "absl/status/statusor.h"

#include "conduit/conduit.h"
#include "conduit/event.h"
#include "conduit/stream/stream.h"

namespace cd {

class ReadFileStream : public ReadStream {
public:
  // Opens a file at the given path as an async readable stream.
  static absl::StatusOr<std::shared_ptr<ReadFileStream>> Open(
    Conduit* conduit,
    const std::filesystem::path& path
  );

  // Sets up the necessary functionality with the given conduit to act as a
  // readable stream on the given file descriptor. It is assumed that the file
  // descriptor is non-blocking.
  //
  // This constructor may be called by users that want to have their own file
  // descriptors and use them like streams. `owning` represents if the stream
  // should take ownership of the file descriptor.
  ReadFileStream(Conduit* conduit, int fd, bool owning = false);
  
private:
  // Not default constructible
  ReadFileStream() = delete;

  void CloseResource() override;

  // Reads up to a certain number of bytes from the file and returns.
  //
  // This function should not retry if interrupted by a signal or the buffer is
  // completely filled.
  void DoRead();
  
  Conduit* conduit_;
  std::shared_ptr<EventListener> listener_;
  int fd_;
  bool owned_;
};

class WriteFileStream : public WriteStream {
public:
  // Opens a file at the given path as an async writable stream.
  //
  // - OpenTrunc opens the file for writing, deleting any data previously
  //   contained.
  // - OpenAppend opens the file for appending to the end of the file.
  //
  // Both functions create previously-non-existent files with the given mode.
  static absl::StatusOr<std::shared_ptr<WriteFileStream>> OpenTrunc(
    Conduit* conduit,
    const std::filesystem::path& path,
    int mode = 0644
  );

  static absl::StatusOr<std::shared_ptr<WriteFileStream>> OpenAppend(
    Conduit* conduit,
    const std::filesystem::path& path,
    int mode = 0644
  );

  // Sets up the necessary functionality with the given conduit to act as a
  // writable stream on the given file descriptor. It is assumed that the file
  // descriptor is non-blocking.
  //
  // This constructor may be called by users that want to have their own file
  // descriptors and use them like streams. `owning` represents if the stream
  // should take ownership of the file descriptor.
  WriteFileStream(Conduit* conduit, int fd, bool owning = false);

private:
  // Not default constructible
  WriteFileStream() = delete;

  size_t TryWriteData(absl::string_view data) override;
  void CloseResource() override;

  Conduit* conduit_;
  std::shared_ptr<EventListener> listener_;
  int fd_;
  bool owned_;
};

}

#endif  // CONDUIT_STREAM_FILE_H_

