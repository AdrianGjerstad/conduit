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
// File: promise.h
// -----------------------------------------------------------------------------
//
// This file declares cd::Promise, a mechanism that more easily facilitates
// asynchronous actions by providing a facility to perform work and then either
// Resolve() or Reject() and allow the consumer of an API to set callbacks for
// those events via Then() and Catch().
//

#ifndef CONDUIT_PROMISE_H_
#define CONDUIT_PROMISE_H_

#include <functional>
#include <memory>

#include "absl/status/status.h"
#include "absl/time/time.h"

#include "conduit/conduit.h"
#include "conduit/timer.h"

namespace cd {

// cd::Promise<T>
//
// T is the type of data that can result from task success. Errors are always
// reported using absl::Status.
template <typename T>
class Promise {
private:
  Conduit* conduit_;
  std::shared_ptr<Timer> timer_;
  bool completed_;

  std::function<void(const T&)> resolve_cb_;
  std::function<void(absl::Status)> reject_cb_;
  std::function<void()> timeout_cb_;

  // Never actually called.
  std::function<void()> depending_closure_;

public:
  // Creates a promise with unbounded time to complete.
  Promise() : conduit_(nullptr), completed_(false) {
    // Nothing to do.
  }

  // Creates a promise with a timeout for resolution or rejection.
  Promise(Conduit* conduit, absl::Duration timeout) : conduit_(conduit),
    completed_(false) {
    // Start a timer
    timer_ = conduit_->OnTimeout(timeout, [this](std::shared_ptr<Timer> t) {
      if (timeout_cb_) {
        timeout_cb_();
      }

      Reject(absl::DeadlineExceededError("operation timed out"));
    });
  }

  // Specify a callback for when this promise succeeds.
  Promise* Then(std::function<void(const T&)> cb) {
    resolve_cb_ = cb;
    return this;
  }

  // Specify a callback for if this promise fails.
  Promise* Catch(std::function<void(absl::Status)> cb) {
    reject_cb_ = cb;
    return this;
  }

  // Specify a callback for if this promise times out.
  //
  // For use by the promise creator only.
  void OnTimeout(std::function<void()> cb) {
    timeout_cb_ = cb;
  }

  // Specify an underlying promise that this promise relies on.
  //
  // For use by the promise creator only. Provided to correct lifetimes.
  template <typename U>
  void DependsOnOther(std::shared_ptr<Promise<U>> other) {
    depending_closure_ = [other]() {
      // Does nothing.
    };
  }

  void Resolve(const T& t) {
    if (!completed_ && resolve_cb_) {
      if (conduit_ && timer_) {
        conduit_->CancelTimer(timer_);
        timer_ = nullptr;
      }

      resolve_cb_(t);
    }

    completed_ = true;
  }

  void Reject(absl::Status e) {
    if (!completed_ && reject_cb_) {
      if (conduit_ && timer_) {
        conduit_->CancelTimer(timer_);
        timer_ = nullptr;
      }

      reject_cb_(e);
    }

    completed_ = true;
  }
};

}

#endif  // CONDUIT_PROMISE_H_

