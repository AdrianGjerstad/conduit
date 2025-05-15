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
// File: event.h
// -----------------------------------------------------------------------------
//
// This file declares cd::EventListener, a class representing a file descriptor
// being listened to and optional std::function's for different types of events
// occurring to those file descriptors.
//

#ifndef CONDUIT_EVENT_H_
#define CONDUIT_EVENT_H_

#include <functional>

namespace cd {

class EventListener {
public:
  // Represents a callable object that takes in a single argument: the file
  // descriptor, and returns nothing. Lambda captures should be used to store
  // more data.
  using ListenerFn = std::function<void(int)>;

  // Not default constructible.
  //
  // An event listener must have a file descriptor attached to it for its entire
  // lifetime. Whether or not that descriptor is valid in kernel-space is a
  // different question that EventListener does not care about.
  EventListener() = delete;

  // Creates a fresh new EventListener for the specified file descriptor.
  EventListener(int fd);

  // Attach event handlers here:
  //
  // - OnXYZ: Allows you to specify a function to be called when XYZ happens.
  // - OffXYZ: Unregisters a previously registered handler function.
  //
  // Events are:
  // - Readable: occurs when read()/recv() have data for reading.
  // - Writable: occurs when write()/send() now have space in their buffers.
  // - Acceptable: occurs when accept() has a new connection available.
  // - Hangup: occurs when the peer of a channel has closed their end.
  //
  // Note, on some implementations, for example, Linux, Readable and Acceptable
  // are the same thing for server sockets. It is implementation-defined which
  // one is invoked first.
  //
  // Calls to these functions after being added to a Conduit are registered with
  // the underlying implementation before waiting for events.
  void OnReadable(ListenerFn listener);
  void OffReadable();
  void OnWritable(ListenerFn listener);
  void OffWritable();
  void OnAcceptable(ListenerFn listener);
  void OffAcceptable();
  void OnHangup(ListenerFn listener);
  void OffHangup();

  // Get whether or not a particular event type has a listener attached.
  bool HasReadable() const;
  bool HasWritable() const;
  bool HasAcceptable() const;
  bool HasHangup() const;

  // Executes the corresponding event callback, if one is available.
  void HandleReadable() const;
  void HandleWritable() const;
  void HandleAcceptable() const;
  void HandleHangup() const;

  // Gets the underlying file descriptor
  int Get() const;

  // Get whether or not a listener callback was added or removed since the last
  // call to this function.
  bool HasListenerSetChanged();

  // Tells Conduit to disregard this listener in determining whether or not to
  // end the program.
  void Disregard();

  // Opposes Disregard(). This is the default.
  void Regard();

  // Calculates whether or not this listener should prevent the application from
  // ending.
  //
  // Currently, as long as the listener is not disregarded and has an
  // OnReadable, OnWritable, or OnAcceptable event listener, the listener will
  // be regarded.
  bool ShouldRegard();

private:
  ListenerFn readable_;
  ListenerFn writable_;
  ListenerFn acceptable_;
  ListenerFn hangup_;
  int fd_;
  bool has_listener_set_changed_;
  bool regarded_;
};

}

#endif  // CONDUIT_EVENT_H_

