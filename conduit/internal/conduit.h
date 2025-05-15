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
// This file declares the implementation interface for a Conduit. In order to
// compile on a new platform, a specific implementation source file must be
// written that implements cd::internal::ConduitImpl in its entirety.
//

#ifndef CONDUIT_INTERNAL_CONDUIT_H_
#define CONDUIT_INTERNAL_CONDUIT_H_

#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/time/time.h"

#include "conduit/event.h"

namespace cd {

namespace internal {

class ConduitImpl {
public:
  ConduitImpl();

  // Corresponds to cd::Conduit::Add and cd::Conduit::Remove, respectively.
  absl::Status Register(int fd, std::shared_ptr<::cd::EventListener> l);
  absl::Status Unregister(int fd, std::shared_ptr<::cd::EventListener> l);

  // Tells if this implementation has listeners that would prevent the
  // application from ending.
  bool HasRegardedListeners();

  // Waits for events to occur and then processes them according to the
  // callbacks provided when the user decided to start listening with an fd.
  //
  // NOT SUPPOSED TO LOOP. After processing all events, this function should
  // return.
  void WaitAndProcessEvents(absl::Duration timeout = absl::InfiniteDuration());

  // Processes all pending events and returns immediately after completion
  //
  // This is equivalent to calling WaitAndProcessEvents with an instantaneous
  // timeout.
  void ProcessPendingEvents() {
    WaitAndProcessEvents(absl::ZeroDuration());
  }

  // Obtains an immutable reference to the internal listeners
  const absl::flat_hash_map<int, std::shared_ptr<::cd::EventListener>>&
    Listeners() const {
    return listeners_;
  }

private:
  // Causes the underlying implementation to refresh this specific listener.
  void Refresh(int fd, std::shared_ptr<::cd::EventListener> l);

  // A set of listeners that can be indexed by their file descriptors
  absl::flat_hash_map<int, std::shared_ptr<::cd::EventListener>> listeners_;
  
  union {
    // Linux-only
    struct {
      // fd returned from epoll_create(2)
      int epfd_;
    } linux_;
  };
};

}

}

#endif  // CONDUIT_INTERNAL_CONDUIT_H_

