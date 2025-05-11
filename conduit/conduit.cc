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
// File: conduit.cc
// -----------------------------------------------------------------------------
//
// This file defines how the public interface for a cd::Conduit interacts with
// the implementation interface cd::internal::ConduitImpl.
//

#include "conduit/conduit.h"

#include <functional>

#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

#include "conduit/event.h"

namespace cd {

absl::Status Conduit::Add(std::shared_ptr<EventListener> listener) {
  // Reset internal flag to avoid unnecessary refreshing immediately after
  // registering this listener.
  listener->HasListenerSetChanged();
  return impl_.Register(listener->Get(), listener);
}

absl::Status Conduit::Remove(std::shared_ptr<EventListener> listener) {
  return impl_.Unregister(listener->Get(), listener);
}

void Conduit::OnNext(std::function<void()> cb) {
  absl::WriterMutexLock lock(&cb_queue_mutex_);

  cb_queue_.push(cb);
}

void Conduit::RunForever() {
  while (IsAlive()) {
    RunCallbackQueue();
    impl_.WaitAndProcessEvents(CalculateTimeout());
  }
}

bool Conduit::IsAlive() {
  {
    absl::ReaderMutexLock lock(&cb_queue_mutex_);
    
    if (cb_queue_.size()) {
      return true;
    }
  }

  // Hangup handlers, while important, must not prevent the application from
  // stopping.
  for (const auto& it : impl_.Listeners()) {
    if (it.second->HasReadable() ||
        it.second->HasWritable() ||
        it.second->HasAcceptable()) {
      return true;
    }
  }

  return false;
}

absl::Duration Conduit::CalculateTimeout() {
  {
    absl::ReaderMutexLock lock(&cb_queue_mutex_);

    if (cb_queue_.size()) {
      // We have outstanding callbacks. Process pending events and get out of
      // there ASAP.
      return absl::ZeroDuration();
    }
  }

  // Clearly nothing else needs to be done, so we can just wait indefinitely.
  return absl::InfiniteDuration();
}

void Conduit::RunCallbackQueue() {
  size_t queue_size;
  {
    absl::ReaderMutexLock read_lock(&cb_queue_mutex_);

    queue_size = cb_queue_.size();
  }

  for (size_t i = 0; i < queue_size; ++i) {
    std::function<void()> fn;
    {
      absl::WriterMutexLock write_lock(&cb_queue_mutex_);

      fn = cb_queue_.front();
      cb_queue_.pop();
    }

    // NOTE: It is imperative that cb_queue_mutex_ is not held by this method
    // when this function is called.
    if (fn) {
      fn();
    }
  }
}

}

