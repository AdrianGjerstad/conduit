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
// File: socket.h
// -----------------------------------------------------------------------------
//
// This file declares cd::TCPSocket, which is a form of cd::DuplexStream that
// implements unencrypted TCP connections. It also declares cd::UDPSocket
// which is simply an interface for sending and receiving UDP packets.
//

#ifndef CONDUIT_NET_SOCKET_H_
#define CONDUIT_NET_SOCKET_H_

#include <functional>
#include <memory>
#include <queue>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include "conduit/conduit.h"
#include "conduit/event.h"
#include "conduit/net/net.h"

namespace cd {

// Represents a socket that uses UDP on the transport layer.
//
// Supposed to be "connected" to or pointed at a host/IP-address and port.
class UDPSocket {
public:
  // Creates a new UDP socket pointing at the given IP address and port.
  //
  // Note: the version of this method that takes a string_view for the host may
  // accept an IPv4 address, an IPv6 address surrounded by '[' and ']', or a
  // hostname. The latter option may be undesirable as it may trigger a DNS
  // query via cd::NameResolver (hence returning a promise).
  static absl::StatusOr<std::shared_ptr<UDPSocket>> Connect(
    Conduit* conduit,
    IPAddress host,
    uint16_t port
  );

  // Constructs a UDP socket that owns the given file descriptor.
  UDPSocket(Conduit* conduit, int fd);

  // Controls the maximum receivable packet length.
  //
  // On just about every platform, attempting to receive a UDP packet that is
  // longer than the given buffer for the packet results in a truncated packet.
  // This generally results in an inability to act on the packet, so its
  // recommended that you set this amount to whatever is necessary for your
  // protocol. The default is 4KiB (4096B).
  size_t MaxPacketLength() const;
  void MaxPacketLength(size_t size);

  // Registers a callback for when a packet is received.
  //
  // Note: a callback must be assigned in the same Conduit runtime loop
  // iteration that the UDPSocket was created in, or else you risk not
  // receiving some packets. It is enough to just call this method after
  // obtaining the UDPSocket pointer for the first time.
  void OnData(std::function<void(absl::string_view)> on_data);

  // Sends a packet to the "peer" of this socket.
  void Write(absl::string_view data);

  // Closes the "connection"/underlying socket.
  void Close();

  // Tells Conduit that this socket is not actively doing anything, and that
  // its okay for the program to end while this socket is open.
  void MarkIdle();

  // Opposes MarkIdle() and tells Conduit that there is outstanding activity on
  // this socket. This is the default behavior.
  void MarkPending();

private:
  // Not default constructible
  UDPSocket() = delete;

  // Receives at most one packet and notifies the user.
  void DoRecv();

  // Attempts to send as many queued packets as possible.
  void Flush();

  Conduit* conduit_;
  std::shared_ptr<cd::EventListener> listener_;
  std::function<void(absl::string_view)> on_data_;
  bool closed_;
  size_t max_pkt_len_;
  int fd_;

  // A queue of packets to be sent once the socket becomes writable again.
  std::queue<std::string> packets_;
};

}

#endif  // CONDUIT_NET_SOCKET_H_

