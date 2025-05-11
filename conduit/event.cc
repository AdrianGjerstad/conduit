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
// File: event.cc
// -----------------------------------------------------------------------------
//
// This file defines the way cd::EventListener works. Note: cd::EventListener is
// a *container* type. It doesn't do any of the implementation-specific system
// calls to actually listen for events. To see that, go to
// conduit/internal/conduit_*.cc.
//

#include "conduit/event.h"

namespace cd {

EventListener::EventListener(int fd) : fd_(fd),
  has_listener_set_changed_(false) {
  // Nothing to do.
}

void EventListener::OnReadable(EventListener::ListenerFn listener) {
  readable_ = listener;
  has_listener_set_changed_ = true;
}

void EventListener::OffReadable() {
  readable_ = nullptr;
  has_listener_set_changed_ = true;
}

void EventListener::OnWritable(EventListener::ListenerFn listener) {
  writable_ = listener;
  has_listener_set_changed_ = true;
}

void EventListener::OffWritable() {
  writable_ = nullptr;
  has_listener_set_changed_ = true;
}

void EventListener::OnAcceptable(EventListener::ListenerFn listener) {
  acceptable_ = listener;
  has_listener_set_changed_ = true;
}

void EventListener::OffAcceptable() {
  acceptable_ = nullptr;
  has_listener_set_changed_ = true;
}

void EventListener::OnHangup(EventListener::ListenerFn listener) {
  hangup_ = listener;
  has_listener_set_changed_ = true;
}

void EventListener::OffHangup() {
  hangup_ = nullptr;
  has_listener_set_changed_ = true;
}

bool EventListener::HasReadable() const {
  return (bool)readable_;
}

bool EventListener::HasWritable() const {
  return (bool)writable_;
}

bool EventListener::HasAcceptable() const {
  return (bool)acceptable_;
}

bool EventListener::HasHangup() const {
  return (bool)hangup_;
}

void EventListener::HandleReadable() const {
  if (readable_) {
    readable_(fd_);
  }
}

void EventListener::HandleWritable() const {
  if (writable_) {
    writable_(fd_);
  }
}

void EventListener::HandleAcceptable() const {
  if (acceptable_) {
    acceptable_(fd_);
  }
}

void EventListener::HandleHangup() const {
  if (hangup_) {
    hangup_(fd_);
  }
}

int EventListener::Get() const {
  return fd_;
}

bool EventListener::HasListenerSetChanged() {
  bool result = has_listener_set_changed_;
  // Returning true would trigger the underlying implementation to refresh its
  // info, so once it does that, this listener set will not have changed since
  // the last refresh.
  has_listener_set_changed_ = false;
  return result;
}

}

