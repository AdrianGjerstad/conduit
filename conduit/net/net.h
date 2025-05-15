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
// File: net.h
// -----------------------------------------------------------------------------
//
// This file declares several utility classes for networking that are used in
// the interfaces of other networking classes.
//

#ifndef CONDUIT_NET_NET_H_
#define CONDUIT_NET_NET_H_

#include <iostream>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace cd {

// Represents an IPv4 or IPv6 address
class IPAddress {
public:
  enum class IPVersion {
    kUnknown = 0,
    k4 = 4,  // IPv4
    k6 = 6,  // IPv6
  };

  // Not default-constructible
  IPAddress() = delete;

  // Creates an IPAddress with the given textual representation.
  //
  // For IPv4 addresses: the string must take the form of w.x.y.z, where each
  // letter is a number from 0 to 255. For IPv6 addresses, the string must take
  // the form of [inet6-addr], where inet6-addr is a valid short-form or
  // long-form IPv6 address, such as [::1].
  static absl::StatusOr<IPAddress> From(absl::string_view s);

  // Creates an IPAddress (version 4) with the given bytes.
  //
  // If the given payload is not 4 bytes, this method returns an error.
  static absl::StatusOr<IPAddress> FromBytes(absl::string_view s);

  // Creates an IPAddress (version 6) with the given bytes.
  //
  // If the given payload is not 16 bytes, this method returns an error.
  static absl::StatusOr<IPAddress> FromBytes6(absl::string_view s);

  // Returns a string suitable to be passed to IPAddress::From.
  std::string ToString() const;

  // Writes the human-readable form of this address to the stream
  friend std::ostream& operator<<(std::ostream& os, const IPAddress& addr);

  // Accessors
  IPVersion Version() const;
  const std::string& AddressAsBytes() const;

  // No manipulators. IPAddress is intended to be immutable.

private:
  // Creates an IPAddress with version v and binary representation s.
  //
  // Example for 127.0.0.1:
  // IPAddress(IPAddress::IPVersion::k4, std::string("\x7f\x00\x00\x01", 4))
  //
  // Method does not do any error checking. Arguments are assigned to members
  // as-is.
  IPAddress(IPVersion v, absl::string_view s);

  IPVersion version_;
  std::string addr_;  // Represented in binary, network-order form.
};

}

#endif  // CONDUIT_NET_NET_H_

