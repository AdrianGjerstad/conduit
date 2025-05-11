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

#include "absl/status/status.h"

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

void Conduit::RunForever() {
  while (true) {
    impl_.WaitAndProcessEvents();
  }
}

}

