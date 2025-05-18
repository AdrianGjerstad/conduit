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
// File: socket_linux.cc
// -----------------------------------------------------------------------------
//
// This file defines the inner workings of both cd::TCPSocket and
// cd::UDPSocket.
//

#include "conduit/net/socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>

#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include "conduit/conduit.h"
#include "conduit/event.h"
#include "conduit/net/net.h"

namespace cd {

absl::StatusOr<std::shared_ptr<UDPSocket>> UDPSocket::Connect(
  Conduit* conduit, IPAddress host, uint16_t port) {
  // Create socket
  int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  if (fd < 0) {
    // All potential errors are either irrecoverable or user error.
    return absl::ErrnoToStatus(errno, "failed to create UDP socket");
  }

  // Assign a peer
  if (host.Version() == IPAddress::IPVersion::k4){
    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    memcpy(&sa.sin_addr.s_addr, host.AddressAsBytes().data(),
           host.AddressAsBytes().size());
    sa.sin_port = htons(port);

    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa))) {
      // Any errors hapenning here are irrecoverable for this socket.
      close(fd);
      return absl::ErrnoToStatus(errno, "failed to assign a UDP peer");
    }
  } else if (host.Version() == IPAddress::IPVersion::k6) {
    struct sockaddr_in6 sa;
    sa.sin6_family = AF_INET6;
    memcpy(sa.sin6_addr.s6_addr, host.AddressAsBytes().data(),
           host.AddressAsBytes().size());
    sa.sin6_port = htons(port);

    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa))) {
      // Any errors hapenning here are irrecoverable for this socket.
      close(fd);
      return absl::ErrnoToStatus(errno, "failed to assign a UDP peer");
    }
  } else {
    return absl::UnimplementedError("unsupported IP version");
  }

  return std::make_shared<UDPSocket>(conduit, fd);
}

absl::StatusOr<std::shared_ptr<UDPSocket>> UDPSocket::Create(Conduit* conduit) {
  // Create socket
  int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  if (fd < 0) {
    // All potential errors are either irrecoverable or user error.
    return absl::ErrnoToStatus(errno, "failed to create UDP socket");
  }

  return std::make_shared<UDPSocket>(conduit, fd);
}

UDPSocket::UDPSocket(Conduit* conduit, int fd) : conduit_(conduit),
  closed_(false), max_pkt_len_(4096), fd_(fd) {
  listener_ = std::make_shared<cd::EventListener>(fd);

  listener_->OnReadable([this](int fd) {
    DoRecv();
  });

  // Only errors are impossible
  conduit_->Add(listener_).IgnoreError();
}

size_t UDPSocket::MaxPacketLength() const {
  return max_pkt_len_;
}

void UDPSocket::MaxPacketLength(size_t size) {
  max_pkt_len_ = size;
}

void UDPSocket::OnData(std::function<void(absl::string_view, IPAddress,
                                          uint16_t)> on_data) {
  on_data_ = on_data;
}

absl::Status UDPSocket::Write(absl::string_view data) {
  if (listener_->HasWritable()) {
    // We're never going to be able to write this data. Queue it.
    packets_.push(std::make_tuple<std::string, IPAddress, uint16_t>(
      std::string(data),
      IPAddress::FromBytes(std::string("\0\0\0\0", 4)).value(), 0
    ));
    return absl::OkStatus();
  }

  ssize_t count = send(fd_, data.data(), data.size(), 0);
  
  if (count < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // kernel buffer must be full
      packets_.push(std::make_tuple<std::string, IPAddress, uint16_t>(
        std::string(data),
        IPAddress::FromBytes(std::string("\0\0\0\0", 4)).value(), 0
      ));
      listener_->OnWritable([this](int fd) {
        Flush();
      });
    } else if (errno == ENOTCONN) {
      return absl::NotFoundError("socket not connected, no known peer");
    }

    // All other errors are impossible.
    return absl::ErrnoToStatus(errno, "send failed");
  }

  // Message was sent successfully!
  return absl::OkStatus();
}

absl::Status UDPSocket::Write(absl::string_view data, IPAddress host,
  uint16_t port) {
  if (port == 0) {
    return absl::InvalidArgumentError("cannot send to UDP port 0");
  }

  if (listener_->HasWritable()) {
    // We're never going to be able to write this data. Queue it.
    packets_.push(std::make_tuple<std::string, IPAddress, uint16_t>(
      std::string(data), IPAddress(host), uint16_t(port)
    ));
    return absl::OkStatus();
  }

  ssize_t count;
  if (host.Version() == IPAddress::IPVersion::k4) {
    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    memcpy(&sa.sin_addr.s_addr, host.AddressAsBytes().data(),
           host.AddressAsBytes().size());
    sa.sin_port = htons(port);

    count = sendto(fd_, data.data(), data.size(), 0, (struct sockaddr*)&sa,
                   sizeof(sa));
  } else if (host.Version() == IPAddress::IPVersion::k6) {
    struct sockaddr_in6 sa;
    sa.sin6_family = AF_INET6;
    memcpy(&sa.sin6_addr.s6_addr, host.AddressAsBytes().data(),
           host.AddressAsBytes().size());
    sa.sin6_port = htons(port);

    count = sendto(fd_, data.data(), data.size(), 0, (struct sockaddr*)&sa,
                   sizeof(sa));
  } else {
    // Nothing can be done. Unknown IP version
    return absl::InvalidArgumentError("unknown IP version");
  }

  if (count < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // kernel buffer must be full
      packets_.push(std::make_tuple<std::string, IPAddress, uint16_t>(
        std::string(data), IPAddress(host), uint16_t(port)
      ));
      listener_->OnWritable([this](int fd) {
        Flush();
      });
    }

    // All other errors are either impossible or caused by user error, such as
    // EMSGSIZE, where the packet was simply far too big.
    return absl::ErrnoToStatus(errno, "sendto failed");
  }

  // Message was sent successfully!
  return absl::OkStatus();
}

void UDPSocket::Close() {
  conduit_->Remove(listener_).IgnoreError();
  close(fd_);
}

void UDPSocket::MarkIdle() {
  listener_->Disregard();
}

void UDPSocket::MarkPending() {
  listener_->Regard();
}

void UDPSocket::DoRecv() {
  std::string buffer(max_pkt_len_, 0);
  struct sockaddr_in6 addr;
  socklen_t addr_len = sizeof(addr);
  ssize_t count = recvfrom(fd_, buffer.data(), buffer.size(), 0,
                           (struct sockaddr*)&addr, &addr_len);

  if (count < 0) {
    // All errors that recv could return are either impossible or simply mean
    // we have other events we need to process before we get back to this,
    // which will happen automatically.
    return;
  }

  if (on_data_) {
    absl::string_view data(buffer);
    data.remove_suffix(buffer.size() - count);
    if (addr.sin6_family == AF_INET) {
      IPAddress ip = IPAddress::FromBytes(std::string(
        (char*)&(((struct sockaddr_in*)&addr)->sin_addr), 4
      )).value();
      on_data_(data, ip, ntohs(((struct sockaddr_in*)&addr)->sin_port));
    } else if (addr.sin6_family == AF_INET6) {
      IPAddress ip = IPAddress::FromBytes6(std::string(
        (char*)&(((struct sockaddr_in6*)&addr)->sin6_addr), 16
      )).value();
      on_data_(data, ip, ntohs(((struct sockaddr_in6*)&addr)->sin6_port));
    }
  }
}

void UDPSocket::Flush() {
  while (packets_.size()) {
    std::tuple<std::string, IPAddress, uint16_t> packet(packets_.front());
    uint16_t port = std::get<2>(packet);
    IPAddress& host = std::get<1>(packet);
    absl::string_view data = std::get<0>(packet);
    ssize_t count;
    if (port == 0) {
      count = send(fd_, data.data(), data.size(), 0);
    } else {
      if (host.Version() == IPAddress::IPVersion::k4) {
        struct sockaddr_in sa;
        sa.sin_family = AF_INET;
        memcpy(&sa.sin_addr.s_addr, host.AddressAsBytes().data(),
               host.AddressAsBytes().size());
        sa.sin_port = htons(port);

        count = sendto(fd_, data.data(), data.size(), 0, (struct sockaddr*)&sa,
                       sizeof(sa));
      } else if (host.Version() == IPAddress::IPVersion::k6) {
        struct sockaddr_in6 sa;
        sa.sin6_family = AF_INET6;
        memcpy(&sa.sin6_addr.s6_addr, host.AddressAsBytes().data(),
               host.AddressAsBytes().size());
        sa.sin6_port = htons(port);

        count = sendto(fd_, data.data(), data.size(), 0, (struct sockaddr*)&sa,
                       sizeof(sa));
      } else {
        // Nothing can be done. Unknown IP version
        packets_.pop();
        continue;
      }
    }

    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // kernel buffer is full again :(
      listener_->OnWritable([this](int fd) {
        Flush();
      });
      return;
    }

    // Only other truly possible error is EINTR (maybe), in which case we just
    // try again next time we enter Flush().
    if (count < 0) {
      return;
    }

    packets_.pop();
  }

  // All packets have been flushed
  listener_->OffWritable();
}

}

