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

#include "conduit/conduit.h"

namespace cd {

// cd::Promise<T>
//
// T is the type of data that can result from task success. Errors are always
// reported using absl::Status.
template <typename T>
class Promise {
public:
  Promise(Conduit* conduit) : conduit_(conduit), completed_(false) {
    // Nothing to do.
  }

  // Specify a callback for when this promise succeeds.
  Promise* Then(std::function<void(std::shared_ptr<T>)> cb) {
    resolve_cb_ = cb;
    return this;
  }

  Promise* Catch(std::function<void(absl::Status)> cb) {
    reject_cb_ = cb;
    return this;
  }

  void Resolve(std::shared_ptr<T> t) {
    if (!completed_ && resolve_cb_) {
      conduit_->OnNext([resolve_cb_, t]() {
        resolve_cb_(t);
      });
    }

    completed_ = true;
  }

  void Reject(absl::Status e) {
    if (!completed_ && reject_cb_) {
      conduit_->OnNext([reject_cb_, e]() {
        reject_cb_(e);
      });
    }

    completed_ = true;
  }

private:
  Conduit* conduit;
  bool completed_;

  std::function<void(T)> resolve_cb_;
  std::function<void(absl::Status)> reject_cb_;
};

}

#endif  // CONDUIT_PROMISE_H_

