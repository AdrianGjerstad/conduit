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
// File: net.cc
// -----------------------------------------------------------------------------
//
// This file defines the implementations of the classes declared in net.h.
//

#include "conduit/net/net.h"

#include <arpa/inet.h>

#include <iostream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace cd {

absl::StatusOr<IPAddress> IPAddress::From(absl::string_view s) {
  absl::Status err = absl::InvalidArgumentError("not a valid IP address");

  // We're using inet_pton here, which takes c-style strings.
  if (s.find('\x00') != absl::string_view::npos) {
    return err;
  }
  
  // Step one: is this an IPv4 or IPv6 address?
  int result;
  if (s.front() == '[' && s.back() == ']') {
    // Probably an IPv6 address
    s.remove_prefix(1);
    s.remove_suffix(1);

    char in6_addr[16];
    result = inet_pton(AF_INET6, std::string(s).c_str(), in6_addr);
    if (result != 1) {
      return err;
    }

    return IPAddress(IPAddress::IPVersion::k6, std::string(in6_addr, 16));
  }

  // Just try to parse it as an IPv4 address.
  char in_addr[4];
  result = inet_pton(AF_INET, std::string(s).c_str(), in_addr);
  if (result != 1) {
    return err;
  }

  return IPAddress(IPAddress::IPVersion::k4, std::string(in_addr, 4));
}

absl::StatusOr<IPAddress> IPAddress::FromBytes(absl::string_view s) {
  if (s.size() != 4) {
    return absl::InvalidArgumentError("not a valid IPv4 address");
  }

  return IPAddress(IPAddress::IPVersion::k4, s);
}

absl::StatusOr<IPAddress> IPAddress::FromBytes6(absl::string_view s) {
  if (s.size() != 16) {
    return absl::InvalidArgumentError("not a valid IPv6 address");
  }

  return IPAddress(IPAddress::IPVersion::k6, s);
}

std::string IPAddress::ToString() const {
  const char* result;
  if (version_ == IPAddress::IPVersion::k4) {
    char in_addr[INET_ADDRSTRLEN];
    result = inet_ntop(AF_INET, addr_.data(), in_addr, sizeof(in_addr));
    
    if (result == NULL) {
      return "<error>";
    }

    return std::string(in_addr);
  } else if (version_ == IPAddress::IPVersion::k6) {
    char in6_addr[INET6_ADDRSTRLEN];
    result = inet_ntop(AF_INET6, addr_.data(), in6_addr, sizeof(in6_addr));

    if (result == NULL) {
      return "<error>";
    }

    return absl::StrFormat("[%s]", std::string(result));
  }
 
  return "<error>";
}

std::ostream& operator<<(std::ostream& os, const IPAddress& addr) {
  os << addr.ToString();
  return os;
}

IPAddress::IPVersion IPAddress::Version() const {
  return version_;
}

const std::string& IPAddress::AddressAsBytes() const {
  return addr_;
}

IPAddress::IPAddress(IPAddress::IPVersion v, absl::string_view s) : version_(v),
  addr_(s) {
  // Nothing to do.
}

}

