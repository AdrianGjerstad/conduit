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
// File: server.h
// -----------------------------------------------------------------------------
//
// This file declares a number of server classes for different protocols,
// including TCP, TLS, and QUIC. For UDP servers, see socket.h, specifically
// cd::UDPSocket::Bind.
//

#ifndef CONDUIT_NET_SERVER_H_
#define CONDUIT_NET_SERVER_H_

#include <functional>
#include <memory>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"

#include "conduit/conduit.h"
#include "conduit/event.h"
#include "conduit/net/net.h"
#include "conduit/net/socket.h"

namespace cd {

// Represents a production-minded TCP server with multiprocessing capabilities.
class TCPServer {
public:
  // Creates a TCPServer with the given host and port.
  static absl::StatusOr<std::shared_ptr<TCPServer>> Create(
    Conduit* conduit,
    IPAddress host,
    uint16_t port,
    bool allow_half_open = false
  );

  // Creates a TCPServer with a server fd that it owns.
  //
  // The fd must be bound but not in listening mode. The server begins listening
  // for connections when Listen() is called. `allow_half_open` is a setting
  // that pertains to TCPSocket instances created and provided to the user when
  // a client connects.
  TCPServer(Conduit* conduit, int fd, bool allow_half_open = false);

  // Begin listening for connections with the underlying socket.
  //
  // This method should not fail under normal circumstances, however there's
  // always the possible EADDRINUSE if something gets messy.
  absl::Status Listen();

  // Assign a callback for when a new connection is successfully accepted.
  void OnConnection(std::function<void(std::shared_ptr<TCPSocket>)> on_conn);

  // Stop this server's listening activities.
  //
  // Also closes all active connections.
  void Shutdown();

private:
  Conduit* conduit_;
  int fd_;
  bool allow_half_open_;
  std::shared_ptr<EventListener> listener_;

  std::function<void(std::shared_ptr<TCPSocket>)> on_conn_;

  absl::flat_hash_set<std::shared_ptr<TCPSocket>> connections_;
};

}

#endif  // CONDUIT_NET_SERVER_H_

