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
// File: main.cc
// -----------------------------------------------------------------------------
//
// This file shows how one could make a basic TCP echo server using Conduit.
//
// The server simply writes what it receives. It can handle multiple concurrent
// connections thanks to Conduit's architecture.
//

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <iostream>
#include <memory>

#include "conduit/conduit.h"
#include "conduit/event.h"

int StartServer() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    std::cout << "socket failed [ERRNO " << errno << ']' << std::endl;
    return -1;
  }

  // Listen on 127.0.0.1:1234
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234);
  addr.sin_addr.s_addr = htonl(0x7f000001);

  if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    std::cout << "bind failed [ERRNO " << errno << ']' << std::endl;
    close(fd);
    return -1;
  }

  if (listen(fd, 4) < 0) {
    std::cout << "listen failed [ERRNO " << errno << ']' << std::endl;
    close(fd);
    return -1;
  }

  return fd;
}

int main(int argc, char** argv) {
  cd::Conduit conduit;

  int server_fd = StartServer();
  if (server_fd < 0) {
    std::cerr << "Failed to start the server" << std::endl;
    return 1;
  }

  auto server_listener = std::make_shared<cd::EventListener>(server_fd);
  server_listener->OnAcceptable([&conduit](int fd) {
    int sock = accept4(fd, NULL, NULL, SOCK_NONBLOCK);
    if (sock < 0) {
      return;
    }

    auto sock_listener = std::make_shared<cd::EventListener>(sock);

    sock_listener->OnReadable([](int fd) {
      constexpr int kMaxCount = 1024;
      char buffer[kMaxCount];
      ssize_t count = read(fd, buffer, kMaxCount);
      if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        std::cerr << "read failed [ERRNO " << errno << ']' << std::endl;
        return;
      }

      // Echo what we got
      int result = write(fd, buffer, count);
      if (result < 0) {
        std::cerr << "write failed [ERRNO " << errno << ']' << std::endl;
      }
    });

    sock_listener->OnHangup([&conduit, &sock_listener](int fd) {
      shutdown(fd, SHUT_RDWR);
      close(fd);
      conduit.Remove(sock_listener).IgnoreError();
    });

    conduit.Add(sock_listener).IgnoreError();
  });

  conduit.Add(server_listener).IgnoreError();

  std::cout << "Listening at 127.0.0.1:1234" << std::endl;

  conduit.Run();
  return 0;
}

