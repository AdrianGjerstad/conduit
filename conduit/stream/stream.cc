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
// File: stream.cc
// -----------------------------------------------------------------------------
//
// This file defines how generic streams work under the hood.
//

#include "conduit/stream/stream.h"

#include <functional>
#include <string>

#include "absl/strings/string_view.h"

namespace cd {

ReadStream::ReadStream() : ended_(false), paused_(true) {
  // Nothing to do.
}

void ReadStream::OnData(std::function<void(absl::string_view)> on_data) {
  on_data_ = on_data;
  Unpause();
}

void ReadStream::OnEnd(std::function<void()> on_end) {
  on_end_ = on_end;
}

std::string ReadStream::Read() {
  std::string data(buffer_);
  buffer_.Clear();
  return data;
}

void ReadStream::Close() {
  CloseResource();

  // This event must be emitted any time the ReadStream ends, no matter what.
  HandleHup();
}

void ReadStream::Pause() {
  paused_ = true;
}

void ReadStream::Unpause() {
  paused_ = false;

  Flush();
}

void ReadStream::Flush() {
  if (paused_ || ended_) {
    return;
  }

  if (buffer_.empty()) {
    return;
  }

  // There is data to be flushed.
  if (on_data_) {
    on_data_(std::string(buffer_));
    buffer_.Clear();
  }
}

void ReadStream::HandleData(absl::string_view data) {
  if (ended_) {
    return;
  }

  if (paused_) {
    // No data is to leave the stream yet.
    buffer_.Append(data);
    return;
  }

  // Not paused, flush the buffer.
  Flush();

  // Now handle data.
  if (on_data_) {
    on_data_(data);
  }
}

void ReadStream::HandleHup() {
  if (ended_) {
    return;
  }

  ended_ = true;

  if (on_end_) {
    on_end_();
  }
}

WriteStream::WriteStream() : closed_(false), corked_(false) {
  // Nothing to do.
}

void WriteStream::OnClose(std::function<void()> on_close) {
  on_close_ = on_close;
}

void WriteStream::Write(absl::string_view data) {
  if (closed_) {
    // We can't write, this stream is closed.
    return;
  }

  buffer_.Append(data);

  if (corked_) {
    // We are corked. No data should be written anywhere other than the buffer.
    return;
  }

  TryFlush();
}

void WriteStream::Close() {
  CloseResource();

  HandleHup();
}

void WriteStream::Cork() {
  corked_ = true;
}

void WriteStream::Uncork() {
  corked_ = false;
  TryFlush();
}

bool WriteStream::TryFlush() {
  if (closed_) {
    return true;
  }

  std::string buffer_str(buffer_);
  buffer_.Clear();
  size_t count = TryWriteData(buffer_str);

  if (count < buffer_str.size()) {
    // Not everything was written
    absl::string_view sub(buffer_str);
    sub.remove_prefix(count);
    buffer_.Append(sub);

    return false;
  }

  return true;
}

void WriteStream::HandleHup() {
  if (closed_) {
    return;
  }

  closed_ = true;

  if (on_close_) {
    on_close_();
  }
}

DuplexStream::DuplexStream(bool allow_half_open) :
  allow_half_open_(allow_half_open), rd_closed_(false), wr_closed_(false),
  paused_(false), corked_(false) {
  // Nothing to do.
}

void DuplexStream::OnData(std::function<void(absl::string_view)> on_data) {
  on_data_ = on_data;
  Unpause();
}

void DuplexStream::OnEnd(std::function<void()> on_end) {
  on_end_ = on_end;
}

std::string DuplexStream::Read() {
  std::string data(rd_buffer_);
  rd_buffer_.Clear();
  return data;
}

void DuplexStream::CloseRead() {
  CloseReadable();

  // This event must be emitted any time the read end ends, no matter what.
  HandleRdHup();

  rd_closed_ = true;
  if (!allow_half_open_ && !wr_closed_) {
    CloseWrite();
  }
}

void DuplexStream::Pause() {
  paused_ = true;
}

void DuplexStream::Unpause() {
  paused_ = false;

  FlushReadEnd();
}

void DuplexStream::OnWriteClose(std::function<void()> on_write_close) {
  on_write_close_ = on_write_close;
}

void DuplexStream::Write(absl::string_view data) {
  if (wr_closed_) {
    // We can't write, write end is closed.
    return;
  }

  wr_buffer_.Append(data);

  if (corked_) {
    // We are corked. No data should be written anywhere other than the buffer.
    return;
  }

  TryFlushWriteEnd();
}

void DuplexStream::CloseWrite() {
  CloseWritable();

  HandleWrHup();

  wr_closed_ = true;
  if (!allow_half_open_ && !rd_closed_) {
    CloseRead();
  }
}

void DuplexStream::Cork() {
  corked_ = true;
}

void DuplexStream::Uncork() {
  corked_ = false;
  TryFlushWriteEnd();
}

void DuplexStream::OnClose(std::function<void()> on_close) {
  on_close_ = on_close;
}

void DuplexStream::Close() {
  CloseWrite();
  CloseRead();
}

void DuplexStream::FlushReadEnd() {
  if (paused_ || rd_closed_) {
    return;
  }

  if (rd_buffer_.empty()) {
    return;
  }

  // There is data to be flushed.
  if (on_data_) {
    on_data_(std::string(rd_buffer_));
    rd_buffer_.Clear();
  }
}

void DuplexStream::HandleData(absl::string_view data) {
  if (rd_closed_) {
    return;
  }

  if (paused_) {
    // No data is to leave the stream yet.
    rd_buffer_.Append(data);
    return;
  }

  // Not paused, flush the buffer.
  FlushReadEnd();

  // Now handle data.
  if (on_data_) {
    on_data_(data);
  }
}

void DuplexStream::HandleRdHup() {
  if (rd_closed_) {
    return;
  }

  rd_closed_ = true;

  if (on_end_) {
    on_end_();
  }

  // Handle full duplex closure event
  if (wr_closed_ && on_close_) {
    on_close_();
  }
}

bool DuplexStream::TryFlushWriteEnd() {
  if (wr_closed_) {
    return true;
  }

  std::string buffer_str(wr_buffer_);
  wr_buffer_.Clear();
  size_t count = TryWriteData(buffer_str);

  if (count < buffer_str.size()) {
    // Not everything was written
    absl::string_view sub(buffer_str);
    sub.remove_prefix(count);
    wr_buffer_.Append(sub);

    return false;
  }

  return true;
}

void DuplexStream::HandleWrHup() {
  if (wr_closed_) {
    return;
  }

  wr_closed_ = true;

  if (on_write_close_) {
    on_write_close_();
  }

  // Handle full duplex closure event
  if (rd_closed_ && on_close_) {
    on_close_();
  }
}

}

