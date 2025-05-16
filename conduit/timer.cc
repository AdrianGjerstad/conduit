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
// File: timer.cc
// -----------------------------------------------------------------------------
//
// This file defines the inner workings of a cd::Timer, which is really just a
// glorified std::function but with additional functions to calculate wait
// times for cd::ConduitImpl.
//

#include "conduit/timer.h"

#include <functional>

#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace cd {

Timer::Timer(TimerMode mode, absl::Duration delta,
  std::function<void()> callback) : mode_(mode), delta_(delta),
  callback_(callback), next_time_(absl::Now() + delta) {
  // Nothing to do.
}

TimerMode Timer::Mode() const {
  return mode_;
}

absl::Duration Timer::Delta() const {
  return delta_;
}

absl::Duration Timer::TimeUntilExpiry() const {
  return next_time_ - absl::Now();
}

void Timer::RunIfExpired() {
  if (TimeUntilExpiry() > absl::ZeroDuration()) {
    // This timer hasn't expired!
    return;
  }

  // This timer has expired. If it's a repeated-shot, we need to set up
  // next_time_.
  switch (mode_) {
  case TimerMode::kDeactivated:
    return;
  case TimerMode::kSingleShot:
    // We're about to run this timer callback. Might as well do this here.
    mode_ = TimerMode::kDeactivated;
    break;
  case TimerMode::kRepeated:
    next_time_ += delta_;
    break;
  default:
    // Error: unrecognized mode
    return;
  }

  // Run the callback
  callback_();
}

}

