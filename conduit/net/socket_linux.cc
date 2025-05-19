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
#include "conduit/net/dns.h"
#include "conduit/net/net.h"
#include "conduit/promise.h"

namespace cd {

absl::StatusOr<std::shared_ptr<UDPSocket>> UDPSocket::Connect(
  Conduit* conduit, IPAddress host, uint16_t port) {
  // Create socket
  int family;
  if (host.Version() == IPAddress::IPVersion::k4) {
    family = AF_INET;
  } else if (host.Version() == IPAddress::IPVersion::k6) {
    family = AF_INET6;
  } else {
    return absl::UnimplementedError("unsupported IP version");
  }

  int fd = socket(family, SOCK_DGRAM | SOCK_NONBLOCK, 0);
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

std::shared_ptr<Promise<std::shared_ptr<UDPSocket>>> UDPSocket::Connect(
  Conduit* conduit, NameResolver* resolver, absl::string_view name,
  uint16_t port) {
  auto promise = std::make_shared<Promise<std::shared_ptr<UDPSocket>>>();

  // Attempt `name` as an IP address first.
  absl::StatusOr<IPAddress> addr_s = IPAddress::From(name);
  if (!addr_s.ok()) {
    // Okay that didn't work, lets try it as a domain name.
    auto resolve_promise = resolver->Resolve(name);
    resolve_promise->Then([conduit, promise, port](
      const std::vector<IPAddress>& addrs) {
      if (addrs.empty()) {
        promise->Reject(absl::NotFoundError("no addresses for that domain"));
        return;
      }

      // Just take the first address.
      absl::StatusOr<std::shared_ptr<UDPSocket>> socket_s =
        UDPSocket::Connect(conduit, addrs[0], port);
      if (!socket_s.ok()) {
        promise->Reject(socket_s.status());
        return;
      }

      promise->Resolve(socket_s.value());
    });
    
    resolve_promise->Catch([promise](absl::Status err) {
      promise->Reject(err);
    });

    promise->DependsOnOther(resolve_promise);

    return promise;
  }

  // IP address parsing was successful
  absl::StatusOr<std::shared_ptr<UDPSocket>> socket_s =
    UDPSocket::Connect(conduit, addr_s.value(), port);

  if (!socket_s.ok()) {
    auto err = socket_s.status();
    conduit->OnNext([promise, err]() {
      promise->Reject(err);
    });
    return promise;
  }

  auto socket = socket_s.value();
  conduit->OnNext([promise, socket]() {
    promise->Resolve(socket);
  });

  return promise;
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

absl::StatusOr<std::shared_ptr<UDPSocket>> UDPSocket::Bind(Conduit* conduit,
  IPAddress host, uint16_t port) {
  // Create socket
  int family;
  if (host.Version() == IPAddress::IPVersion::k4) {
    family = AF_INET;
  } else if (host.Version() == IPAddress::IPVersion::k6) {
    family = AF_INET6;
  } else {
    return absl::UnimplementedError("unsupported IP version");
  }

  int fd = socket(family, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  if (fd < 0) {
    // All potential errors are either irrecoverable or user error.
    return absl::ErrnoToStatus(errno, "failed to create UDP socket");
  }

  // Bind socket
  if (host.Version() == IPAddress::IPVersion::k4){
    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    memcpy(&sa.sin_addr.s_addr, host.AddressAsBytes().data(),
           host.AddressAsBytes().size());
    sa.sin_port = htons(port);

    if (bind(fd, (struct sockaddr*)&sa, sizeof(sa))) {
      // Any errors hapenning here are irrecoverable for this socket.
      close(fd);
      return absl::ErrnoToStatus(errno, "failed to bind UDP socket");
    }
  } else if (host.Version() == IPAddress::IPVersion::k6) {
    struct sockaddr_in6 sa;
    sa.sin6_family = AF_INET6;
    memcpy(sa.sin6_addr.s6_addr, host.AddressAsBytes().data(),
           host.AddressAsBytes().size());
    sa.sin6_port = htons(port);

    if (bind(fd, (struct sockaddr*)&sa, sizeof(sa))) {
      // Any errors hapenning here are irrecoverable for this socket.
      close(fd);
      return absl::ErrnoToStatus(errno, "failed to bind UDP socket");
    }
  } else {
    return absl::UnimplementedError("unsupported IP version");
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

std::shared_ptr<Promise<std::shared_ptr<TCPSocket>>> TCPSocket::Connect(
  Conduit* conduit, IPAddress host, uint16_t port, bool allow_half_open) {
  auto promise = std::make_shared<Promise<std::shared_ptr<TCPSocket>>>();
  
  // Create the socket
  int family;
  if (host.Version() == IPAddress::IPVersion::k4) {
    family = AF_INET;
  } else if (host.Version() == IPAddress::IPVersion::k6) {
    family = AF_INET6;
  } else {
    absl::Status err = absl::UnimplementedError("unsupported IP version");
    conduit->OnNext([promise, err]() {
      promise->Reject(err);
    });
    return promise;
  }

  int fd = socket(family, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (fd < 0) {
    absl::Status err = absl::ErrnoToStatus(errno, "failed to create socket");
    conduit->OnNext([promise, err]() {
      promise->Reject(err);
    });
    return promise;
  }

  // Connect to the peer
  int res;
  if (host.Version() == IPAddress::IPVersion::k4){
    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    memcpy(&sa.sin_addr.s_addr, host.AddressAsBytes().data(),
           host.AddressAsBytes().size());
    sa.sin_port = htons(port);

    while ((res = connect(fd, (struct sockaddr*)&sa, sizeof(sa))) &&
           errno == EINTR);
  } else if (host.Version() == IPAddress::IPVersion::k6) {
    struct sockaddr_in6 sa;
    sa.sin6_family = AF_INET6;
    memcpy(sa.sin6_addr.s6_addr, host.AddressAsBytes().data(),
           host.AddressAsBytes().size());
    sa.sin6_port = htons(port);

    while ((res = connect(fd, (struct sockaddr*)&sa, sizeof(sa))) &&
           errno == EINTR);
  } else {
    absl::Status err = absl::UnimplementedError("unsupported IP version");
    conduit->OnNext([promise, err]() {
      promise->Reject(err);
    });
    return promise;
  }

  if (res < 0) {
    if (errno == EINPROGRESS) {
      // TCP connection attempt started, we just need to wait for results.
      auto listener = std::make_shared<EventListener>(fd);
      listener->OnWritable([conduit, promise, listener, allow_half_open](
        int fd) {
        conduit->Remove(listener).IgnoreError();
        int err;
        socklen_t errlen = sizeof(err);

        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen)) {
          promise->Reject(absl::InternalError(
            "failed to check connection status after attempting to connect"
          ));
        }

        if (err == 0) {
          promise->Resolve(std::make_shared<TCPSocket>(conduit, fd,
                                                       allow_half_open));
          return;
        }

        promise->Reject(absl::ErrnoToStatus(err, "failed to connect"));
      });

      conduit->Add(listener).IgnoreError();
      return promise;
    }

    // Something happened that caused connect to fail way early.
    absl::Status err = absl::ErrnoToStatus(errno, "failed to connect");
    conduit->OnNext([promise, err]() {
      promise->Reject(err);
    });

    return promise;
  }

  // For some weird reason, the connection succeeded after just calling connect,
  // even though the socket is non-blocking.
  conduit->OnNext([conduit, promise, fd, allow_half_open]() {
    promise->Resolve(std::make_shared<TCPSocket>(conduit, fd, allow_half_open));
  });
  return promise;
}

TCPSocket::TCPSocket(Conduit* conduit, int fd, bool allow_half_open) :
  DuplexStream(allow_half_open), conduit_(conduit), fd_(fd),
  half_closed_(false) {
  listener_ = std::make_shared<EventListener>(fd_);

  listener_->OnReadable([this](int fd) {
    DoRecv();
  });

  listener_->OnHangup([this](int fd) {
    HandleRdHup();
  });

  conduit_->Add(listener_).IgnoreError();
}

void TCPSocket::FullClose() {
  conduit_->Remove(listener_).IgnoreError();
  close(fd_);
}

void TCPSocket::CloseReadable() {
  half_closed_ = !half_closed_;
  shutdown(SHUT_RD, fd_);
  if (!half_closed_) {
    FullClose();
  }
}

void TCPSocket::CloseWritable() {
  half_closed_ = !half_closed_;
  shutdown(SHUT_WR, fd_);
  if (!half_closed_) {
    FullClose();
  }
}

size_t TCPSocket::TryWriteData(absl::string_view data) {
  ssize_t count = send(fd_, data.data(), data.size(), 0);

  if (count < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // We must set an OnWritable listener. The kernel buffer must be full.
      listener_->OnWritable([this](int fd) {
        TryFlushWriteEnd();
      });
    }

    return 0;
  }

  if ((size_t)count < data.size()) {
    // Not everything was sent. The kernel buffer must be full.
    listener_->OnWritable([this](int fd) {
      TryFlushWriteEnd();
    });
    
    return count;
  }

  // Everything was written.
  if (listener_->HasWritable()) {
    listener_->OffWritable();
  }

  return count;
}

void TCPSocket::DoRecv() {
  std::string buffer(4096, 0);
  ssize_t count = recv(fd_, buffer.data(), buffer.size(), 0);

  if (count < 0) {
    // No errors should result because this method should only be called when
    // data is available.
    return;
  }

  absl::string_view view(buffer);
  view.remove_suffix(buffer.size() - count);
  HandleData(view);
}

}

