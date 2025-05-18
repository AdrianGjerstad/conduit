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
// File: net_linux.cc
// -----------------------------------------------------------------------------
//
// This file defines all generic networking subroutines that are
// platform-dependent, specifically for Linux.
//

#include "conduit/net/net.h"

#include <errno.h>

#include <fstream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace cd {

absl::StatusOr<IPAddress> GetGatewayAddressSync() {
  // /proc/net/route has our answer
  std::ifstream is("/proc/net/route");

  if (!is.is_open()) {
    return absl::ErrnoToStatus(errno,
                               "failed to open /proc/net/route for reading");
  }

  std::string iface;
  std::string destination;
  std::string gateway;

  // Skip header line
  std::getline(is, iface);

  while (!is.eof()) {
    // Get fields from each line and determine if it is the default interface
    is >> iface >> destination >> gateway;

    auto pos = destination.find_first_not_of('0');
    if (pos != std::string::npos) {
      // Not here
      continue;
    }

    // This is the default iface!
    uint32_t gwaddr = std::stoul(gateway, nullptr, 16);

    std::string gw("\x00\x00\x00\x00", 4);

    gw[0] = static_cast<uint8_t>(gwaddr & 0xFF);
    gw[1] = static_cast<uint8_t>((gwaddr >> 8) & 0xFF);
    gw[2] = static_cast<uint8_t>((gwaddr >> 16) & 0xFF);
    gw[3] = static_cast<uint8_t>(gwaddr >> 24);
    return IPAddress::FromBytes(gw).value();
  }

  return absl::UnavailableError("no default interface found");
}

}

