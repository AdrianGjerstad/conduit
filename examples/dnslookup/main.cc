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
// This file demonstrates how to perform an asynchronous DNS lookup.
//

#include <iostream>
#include <string>
#include <vector>

#include "conduit/conduit.h"
#include "conduit/event.h"
#include "conduit/net/dns.h"
#include "conduit/net/net.h"

int main(int argc, char** argv) {
  cd::Conduit conduit;
  cd::NameResolver resolver(&conduit);

  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <domain-name>" << std::endl;
    return 1;
  }

  auto promise = resolver.Resolve(std::string(argv[1]));
  promise->Then([argv, &resolver](const std::vector<cd::IPAddress>& addrs) {
    std::cout << argv[1] << " has the following IP addresses:" << std::endl;
    for (const auto& addr : addrs) {
      std::cout << addr << std::endl;
    }
  });
  
  promise->Catch([](absl::Status err) {
    std::cerr << "Error while resolving: " << err << std::endl;
  });

  conduit.Run();
  return 0;
}

