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
// This file demonstrates how to open a TCP connection.
//
// To properly test this example, you should start the echoserver example first.
//

#include <memory>

#include "absl/status/status.h"
#include "conduit/conduit.h"
#include "conduit/net/dns.h"
#include "conduit/net/net.h"
#include "conduit/net/socket.h"
#include "conduit/promise.h"

int main(int argc, char** argv) {
  cd::Conduit conduit;
  cd::NameResolver resolver(&conduit);

  auto promise = cd::TCPSocket::Connect(
    &conduit,
    &resolver,
    "tcpbin.com",
    4242
  );

  promise->Then([](std::shared_ptr<cd::TCPSocket> socket) {
    socket->OnData([socket](absl::string_view data) {
      std::cout << "Received: " << data;
      socket->Close();
    });

    socket->Write("Hello, world!\n");
    std::cout << "Sent: Hello, world!\n";
  });

  promise->Catch([](absl::Status err) {
    std::cerr << "failed to connect: " << err << std::endl;
  });

  conduit.Run();
  return 0;
}

