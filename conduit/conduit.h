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

#include <memory>

#include "absl/status/status.h"

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

  // Begins listening for events on this Conduit
  void RunForever();

private:
  // To allow for non-Linux implementations, implementation details are
  // abstracted away.
  internal::ConduitImpl impl_;
};

}

#endif  // CONDUIT_CONDUIT_H_

