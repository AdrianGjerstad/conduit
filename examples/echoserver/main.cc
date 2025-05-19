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

#include <iostream>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "conduit/conduit.h"
#include "conduit/net/net.h"
#include "conduit/net/server.h"
#include "conduit/net/socket.h"

int main(int argc, char** argv) {
  cd::Conduit conduit;

  absl::StatusOr<std::shared_ptr<cd::TCPServer>> server_s =
    cd::TCPServer::Create(
      &conduit,
      cd::IPAddress::From("127.0.0.1").value(),
      1234
    );

  if (!server_s.ok()) {
    std::cerr << "Failed to create server: " << server_s.status() << std::endl;
    return 1;
  }

  auto server = server_s.value();

  server->OnConnection([](std::shared_ptr<cd::TCPSocket> socket) {
    socket->OnData([socket](absl::string_view data) {
      socket->Write(data);
    });
  });

  absl::Status err = server->Listen();
  if (!err.ok()) {
    server->Shutdown();
    std::cerr << "Failed to listen: " << err << std::endl;
    return 1;
  }

  std::cout << "Listening at 127.0.0.1:1234" << std::endl;

  conduit.Run();
  return 0;
}

