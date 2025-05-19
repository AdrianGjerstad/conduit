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
// File: server_linux.cc
// -----------------------------------------------------------------------------
//
// This file defines the inner workings of various servers, specifically their
// platform-dependent parts.
//

#include "conduit/net/server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>

#include <functional>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "conduit/conduit.h"
#include "conduit/event.h"
#include "conduit/net/net.h"
#include "conduit/net/socket.h"

namespace cd {

absl::StatusOr<std::shared_ptr<TCPServer>> TCPServer::Create(Conduit* conduit,
  IPAddress host, uint16_t port, bool allow_half_open) {
  // Create socket
  int family;
  if (host.Version() == IPAddress::IPVersion::k4) {
    family = AF_INET;
  } else if (host.Version() == IPAddress::IPVersion::k6) {
    family = AF_INET6;
  } else {
    return absl::UnimplementedError("unrecognized IP version");
  }

  int fd = socket(family, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (fd < 0) {
    return absl::ErrnoToStatus(errno, "failed to create socket");
  }

  // Bind to address
  if (host.Version() == IPAddress::IPVersion::k4) {
    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    memcpy(&sa.sin_addr.s_addr, host.AddressAsBytes().data(),
           host.AddressAsBytes().size());
    sa.sin_port = htons(port);

    if (bind(fd, (struct sockaddr*)&sa, sizeof(sa))) {
      // Any errors are irrecoverable
      close(fd);
      return absl::ErrnoToStatus(errno, "failed to bind address");
    }
  } else if (host.Version() == IPAddress::IPVersion::k6) {
    struct sockaddr_in6 sa;
    sa.sin6_family = AF_INET6;
    memcpy(&sa.sin6_addr.s6_addr, host.AddressAsBytes().data(),
           host.AddressAsBytes().size());
    sa.sin6_port = htons(port);

    if (bind(fd, (struct sockaddr*)&sa, sizeof(sa))) {
      // Any errors are irrecoverable
      close(fd);
      return absl::ErrnoToStatus(errno, "failed to bind address");
    }
  }

  // Socket created and bound successfully
  return std::make_shared<TCPServer>(conduit, fd, allow_half_open);
}

TCPServer::TCPServer(Conduit* conduit, int fd, bool allow_half_open) :
  conduit_(conduit), fd_(fd), allow_half_open_(allow_half_open) {
  listener_ = std::make_shared<EventListener>(fd_);

  conduit_->Add(listener_).IgnoreError();
}

absl::Status TCPServer::Listen() {
  int res = listen(fd_, 16);
  if (res < 0) {
    return absl::ErrnoToStatus(errno, "failed to listen");
  }

  // Add acceptable listener
  listener_->OnAcceptable([this](int fd) {
    int conn_fd = accept4(fd_, NULL, NULL, SOCK_NONBLOCK);
    if (conn_fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // This can happen in multiprocessed servers. Simply put, a race
        // happened between this process and another to accept the new
        // connection, and this process lost. No biggie.
        return;
      }

      // Nothing we can do about errors here. All we know is that there is no
      // new connection.
      return;
    }

    auto socket = std::make_shared<TCPSocket>(conduit_, conn_fd,
                                              allow_half_open_);
    connections_.insert(socket);

    socket->OnCloseAlertServer([this, socket]() {
      connections_.erase(socket);
    });

    if (on_conn_) {
      on_conn_(socket);
    }
  });

  return absl::OkStatus();
}

void TCPServer::OnConnection(std::function<void(std::shared_ptr<TCPSocket>)>
                             on_conn) {
  on_conn_ = on_conn;
}

void TCPServer::Shutdown() {
  // Prevent accepting new connections
  listener_->OffAcceptable();

  // Close all connections
  for (auto& conn : connections_) {
    conn->Close();
    connections_.erase(conn);
  }

  connections_.clear();

  // Close server
  conduit_->Remove(listener_).IgnoreError();
  close(fd_);
}

}

