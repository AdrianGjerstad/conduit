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
// File: stream.h
// -----------------------------------------------------------------------------
//
// This file declares three different kinds of character stream abstractions.
// (1) cd::ReadStream - A half-duplex character stream that reads outstanding
//     data until closed.
// (2) cd::WriteStream - A half-duplex character stream that maintains a write
//     buffer and attempts to flush that buffer as fast as possible without
//     blocking.
// (3) cd::DuplexStream - A full-duplex character stream that does what both
//     cd::ReadStream and cd::WriteStream do. Inherits from both classes so that
//     methods expecting half-duplex streams can accept full-duplex.
//

#ifndef CONDUIT_STREAM_STREAM_H_
#define CONDUIT_STREAM_STREAM_H_

#include <functional>
#include <string>

#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"

namespace cd {

// Describes a readable stream of characters.
//
// Implementors must:
// - Implement CloseResource() as a function to close the underlying resource.
// - Call HandleData() when data is available.
// - Call HandleHup() when the underlying resource is closed for some reason
//   other than the user manually closing it.
class ReadStream {
public:
  ReadStream();

  // Assigns a data listener for this ReadStream.
  //
  // Similar to Node.js's `stream.on("data", function(chunk) ...)`. The stream
  // is paused when constructed, but is unpaused automatically when this
  // callback is set.
  void OnData(std::function<void(absl::string_view)> on_data);

  // Assigns a read-end hangup listener for this ReadStream.
  void OnEnd(std::function<void()> on_end);

  // Reads as much data as possible from the buffer.
  //
  // This is the alternative to setting OnData, but has the pitfall of having to
  // be called in order to retrieve the data, which is not conducive to
  // asynchronicity.
  std::string Read();

  // Closes the ReadStream
  void Close();

  // Prevents the data callback from getting called until Unpause is called.
  // Note: by default the stream is Pause'd. See OnData().
  void Pause();
  
  // Resumes the regular flow of data.
  void Unpause();

protected:
  // Flushes internal buffer if the stream is not paused.
  void Flush();
  // Tells the user that data is available.
  void HandleData(absl::string_view data);

  // Tells the user that this ReadStream is now closed.
  void HandleHup();

  virtual void CloseResource() = 0;

private:
  std::function<void(absl::string_view)> on_data_;
  std::function<void()> on_end_;
  bool ended_;

  // This is where data gets buffered if the stream is paused.
  absl::Cord buffer_;

  bool paused_;
};

// Describes a writable stream of characters.
//
// Implementors must:
// - Implement TryWriteData() as a function to try to write a chunk of data.
// - Implement CloseResource() as a function to close the underlying resource.
// - Call HandleHup() when the underlying resource closes for some reason other
//   than the user manually closing it.
class WriteStream {
public:
  WriteStream();

  // Assigns an event handler for when the WriteStream ends.
  void OnClose(std::function<void()> on_close);

  // Writes data to the stream.
  void Write(absl::string_view data);

  // Closes the WriteStream
  void Close();

  // Prevents data from being flushed to the underlying resource.
  void Cork();

  // Resumes the regular flow of data.
  void Uncork();

protected:
  // Attempts to flush the internal buffer to the underlying resource.
  //
  // Returns true if the entire buffer was flushed, false if data still remains.
  bool TryFlush();

  // Attempts to write the given data to the underlying resource.
  //
  // Returns the number of bytes that were actually written to the resource. If
  // the count is less than data.size(), the implementation is responsible for
  // doing what it needs to to call TryFlush() when more data can be written.
  // For example, with a cd::EventListener, TryFlush should be called inside
  // OnWritable.
  virtual size_t TryWriteData(absl::string_view data) = 0;

  // Tells the user that this WriteStream is now closed.
  void HandleHup();

  virtual void CloseResource() = 0;

private:
  std::function<void()> on_close_;
  bool closed_;

  absl::Cord buffer_;

  bool corked_;
};

// Describes a duplex stream of characters.
//
// This class is not just a pairing of a ReadStream and a WriteStream that
// access the same resource because a DuplexStream is supposed to be an
// abstraction on top of a single resource that is both readable and writable,
// and not a pair of readable and writable resources.
//
// Implementors must:
// - Implement CloseReadable() as a function that closes the read-end of the
//   underlying resource
// - Implement CloseWritable() as a function that closes the write-end of the
//   underlying resource
// - Call HandleData() when data is available.
// - Call HandleRdHup() when the underlying resource ends for some reason other
//   than the user manually closing it.
// - Implement TryWriteData() as a function to try to write a chunk of data.
// - Call HandleWrHup() when the underlying resource closes for some reason
//   other than the user manually closing it.
class DuplexStream {
public:
  // Constructs a new DuplexStream with the following options:
  //
  // - allow_half_open: if true, allows one end of the stream to be open while
  //   the other is closed. If false, automatically closes the other end when
  //   one closes.
  DuplexStream(bool allow_half_open = false);

  // See ReadStream::OnData
  void OnData(std::function<void(absl::string_view)> on_data);

  // See ReadStream::OnEnd. Pertains *specifically* to the read end.
  void OnEnd(std::function<void()> on_end);

  // See ReadStream::Read
  std::string Read();

  // See ReadStream::Close. Closes both ends if allow_half_open is false.
  void CloseRead();

  // See ReadStream::Pause
  void Pause();
  
  // See ReadStream::Unpause
  void Unpause();

  // See WriteStream::OnWriteClose. Pertains *specifically* to the write end.
  void OnWriteClose(std::function<void()> on_write_close);

  // See WriteStream::Write
  void Write(absl::string_view data);

  // See WriteStream::Close. Closes both ends if allow_half_open is false.
  void CloseWrite();

  // See WriteStream::Cork
  void Cork();

  // See WriteStream::Uncork
  void Uncork();

  // Assigns a handler to be executed when both the read and write ends have
  // closed.
  void OnClose(std::function<void()> on_close);

  // Closes both ends of the stream
  void Close();

protected:
  // Flushes internal read buffer if the stream is not paused.
  void FlushReadEnd();
  // Tells the user that data is available.
  void HandleData(absl::string_view data);

  // Tells the user that the read-end of this duplex is now closed.
  void HandleRdHup();

  virtual void CloseReadable() = 0;

  // Attempts to flush the internal write buffer to the underlying resource.
  //
  // Returns true if the entire buffer was flushed, false if data still remains.
  bool TryFlushWriteEnd();

  // See WriteStream::TryWriteData
  virtual size_t TryWriteData(absl::string_view data) = 0;

  // Tells the user that the write-end of this duplex is now closed.
  void HandleWrHup();

  virtual void CloseWritable() = 0;

private:
  bool allow_half_open_;
  
  std::function<void(absl::string_view)> on_data_;
  std::function<void()> on_end_;
  std::function<void()> on_write_close_;
  std::function<void()> on_close_;
  bool rd_closed_;
  bool wr_closed_;

  // This is where data gets buffered if the stream is paused.
  absl::Cord rd_buffer_;
  // This is where data gets buffered if the stream is corked.
  absl::Cord wr_buffer_;

  bool paused_;
  bool corked_;
};

}

#endif  // CONDUIT_STREAM_STREAM_H_

