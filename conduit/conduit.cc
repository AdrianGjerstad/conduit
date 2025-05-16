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
#include <memory>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "conduit/event.h"
#include "conduit/timer.h"

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

std::shared_ptr<Timer> Conduit::OnTimeout(absl::Duration delta,
  std::function<void()> cb) {
  auto timer = std::make_shared<Timer>(TimerMode::kSingleShot, delta, cb);

  {
    absl::WriterMutexLock lock(&timer_set_mutex_);

    timers_.insert(timer);
  }

  return timer;
}

std::shared_ptr<Timer> Conduit::OnInterval(absl::Duration delta,
  std::function<void()> cb) {
  auto timer = std::make_shared<Timer>(TimerMode::kRepeated, delta, cb);

  {
    absl::WriterMutexLock lock(&timer_set_mutex_);

    timers_.insert(timer);
  }

  return timer;
}

void Conduit::CancelTimer(std::shared_ptr<Timer> timer) {
  absl::WriterMutexLock lock(&timer_set_mutex_);

  timers_.erase(timer);
}

void Conduit::Run() {
  while (IsAlive()) {
    RunCallbackQueue();
    if (impl_.HasRegardedListeners()) {
      impl_.WaitAndProcessEvents(CalculateTimeout());
    }
    RunExpiredTimers();
  }
}

bool Conduit::IsAlive() {
  {
    absl::ReaderMutexLock lock(&cb_queue_mutex_);
    
    if (cb_queue_.size()) {
      return true;
    }
  }

  {
    absl::ReaderMutexLock lock(&timer_set_mutex_);

    if (!timers_.empty()) {
      return true;
    }
  }

  return impl_.HasRegardedListeners();
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

  {
    absl::ReaderMutexLock lock(&timer_set_mutex_);

    absl::Duration min_remaining = absl::InfiniteDuration();
    for (const auto& timer : timers_) {
      absl::Duration remaining = timer->TimeUntilExpiry();
      if (remaining < absl::ZeroDuration()) {
        return absl::ZeroDuration();
      }

      if (min_remaining > remaining) {
        min_remaining = remaining;
      }
    }

    return min_remaining;
  }
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

void Conduit::RunExpiredTimers() {
  absl::flat_hash_set<std::shared_ptr<Timer>> expired_timers;
  {
    absl::ReaderMutexLock read_lock(&timer_set_mutex_);
    for (auto& timer : timers_) {
      if (timer->TimeUntilExpiry() <= absl::ZeroDuration()) {
        expired_timers.insert(timer);
      }
    }
  }

  for (auto& timer : expired_timers) {
    timer->RunIfExpired();

    if (timer->Mode() == TimerMode::kDeactivated) {
      // RunIfExpired would no longer do anything.
      absl::WriterMutexLock write_lock(&timer_set_mutex_);
      timers_.erase(timer);
    }
  }
}

}

