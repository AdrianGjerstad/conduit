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
// File: timer.h
// -----------------------------------------------------------------------------
//
// This file declares cd::Timer, a construct that enables time-based events. All
// timing calculations are based on the original call for their creation. Thus,
// they are as stable as possible as long as the user does not block the event
// loop.
//
// cd::Timer has two modes: single-shot, and repeated. While implementation-
// dependent, in the interest of avoiding memory bloat, underlying Conduit
// implementations SHOULD calculate a timeout for the next Timer that will
// expire so that when the thread goes to sleep to wait for events, it will be
// woken up if a Timer needs to expire. Both modes of operation are described
// below.
//
// Single-Shot Timers
//
//   Single-shot timers may serve many different purposes throughout a codebase,
//   but their aim is simple: run a callback after a timeout has expired, and
//   then do nothing.
//
// Repeated Timers
//
//   Also known as interval timers, these timers are made to execute a callback
//   repeatedly after an interval has passed, taking into account work done by
//   the callback and other callbacks in the system. It is for this reason that
//   the function of repeated timers cannot be replicated with single-shot
//   timers.
//
// The user should not create timers themself, but rather should use
// Conduit::OnTimeout and Conduit::OnInterval for single-shot and repeated
// timers respectively.
//
// The time-accuracy of callbacks is dependent on many factors, such as pending
// work to be done, context switches and OS work, and the precision of the
// underlying clock.
//

#ifndef CONDUIT_TIMER_H_
#define CONDUIT_TIMER_H_

#include <functional>
#include <memory>

#include "absl/time/time.h"

namespace cd {

// Represents the mode of operation for a timer.
enum class TimerMode {
  // A single-shot timer that has, since creation, had its callback called.
  kDeactivated = 1,
  // A single-shot timer that has yet to expire.
  kSingleShot = 2,
  // A repeated timer.
  kRepeated = 3,
};

class Timer {
public:
  // Not default-constructible
  Timer() = delete;

  // Creates a timer with a callback to execute after a delta has passed.
  Timer(TimerMode mode, absl::Duration delta,
    std::function<void(std::shared_ptr<Timer>)> callback);

  // These attributes of a timer are designed to be immutable (externally).
  TimerMode Mode() const;
  absl::Duration Delta() const;

  // For scheduling, calculates the amount of time that must pass before the
  // encapsulated callback can be invoked. If the result is zero or negative,
  // assume that the callback should be invoked (unless the timer is
  // deactivated).
  absl::Duration TimeUntilExpiry() const;

  // Invokes the callback if this Timer has expired.
  //
  // Also handles resetting internal state for the next invocation on repeated
  // timers.
  void RunIfExpired(std::shared_ptr<Timer> self);

private:
  TimerMode mode_;
  absl::Duration delta_;
  std::function<void(std::shared_ptr<Timer>)> callback_;
  // Represents the next time that the callback should be called.
  absl::Time next_time_;
};

}

#endif  // CONDUIT_TIMER_H_

