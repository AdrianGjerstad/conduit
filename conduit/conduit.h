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
// File: conduit.h
// -----------------------------------------------------------------------------
//
// This file declares cd::Conduit, an event loop-style driver for handling
// events.
//

#ifndef CONDUIT_CONDUIT_H_
#define CONDUIT_CONDUIT_H_

#include <functional>
#include <memory>
#include <queue>

#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

#include "conduit/event.h"
#include "conduit/internal/conduit.h"

namespace cd {

class Conduit {
public:
  Conduit() = default;

  // Not copy-constructible (Conduits should never be copied)
  Conduit(const Conduit& c) = delete;
  // Not copy-assignable
  Conduit& operator=(const Conduit& c) = delete;

  // Registers an event listener with this Conduit.
  //
  // Fails if the underlying file descriptor is already registered, or an
  // implementation-specific error occurs, likely arising from using certain
  // file descriptors.
  absl::Status Add(std::shared_ptr<EventListener> listener_);

  // Unregisters an event listener with this Conduit.
  //
  // Fails if the underlying file descriptor is not registered.
  absl::Status Remove(std::shared_ptr<EventListener> listener_);

  // Adds a callback to the queue of functions to be called at the start of the
  // next loop iteration.
  //
  // THREAD SAFETY: This function mutates internal Conduit state, and obtains
  // a *writing* lock to do so.
  void OnNext(std::function<void()> cb);

  // Begins listening for events and running callbacks on this Conduit
  void Run();

private:
  // Checks if the loop should start its next iteration or if the program should
  // end.
  bool IsAlive();

  // Calculates the maximum amount of time that the loop is allowed to wait for
  // for events before the thread must wake back up to do work.
  absl::Duration CalculateTimeout();

  // Executes the current contents of the callback queue.
  //
  // Allows for the likely possibility of more callback work being scheduled
  // through these jobs.
  //
  // THREAD SAFETY: This function accesses internal Conduit state, and obtains
  // a writing lock *only* while popping callbacks off of the front of the
  // queue. It also may obtain a reading lock at any time that doesn't involve
  // calling user-specified code.
  void RunCallbackQueue();

  // To allow for non-Linux implementations, implementation details are
  // abstracted away.
  internal::ConduitImpl impl_;

  // Queue of callbacks to be executed at the start of the next loop.
  std::queue<std::function<void()>> cb_queue_;
  absl::Mutex cb_queue_mutex_;
};

}

#endif  // CONDUIT_CONDUIT_H_

