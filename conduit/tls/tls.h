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
// File: tls.h
// -----------------------------------------------------------------------------
//
// This file declares a number of SSL/TLS facilities for use in secure
// communications networking.
//
// Conduit's original purpose was to create an environment in which developers
// could run code asynchronously, based off of various I/O, timing, and signal
// events. It has since grown to become a starting point for any application
// requiring asynchronicity, as it provides many interfaces to perform these
// basic, and often OS-specific operations. Conduit's TLS interfaces are one
// such example.
//
// As of right now, and for the forseeable future, Conduit's implementation of
// TLS *is* OpenSSL, not just based on it. OpenSSL is a time/production-tested
// library that is considered safe for any environment given proper
// configuration. Conduit is designed to use OpenSSL in a production-ready
// manner, ensuring that the user can't shoot themselves in the foot. For
// example, broken algorithms/protocol versions are just completely disabled for
// the safety of the service.
//

#ifndef CONDUIT_TLS_TLS_H_
#define CONDUIT_TLS_TLS_H_

#include "absl/status/statusor.h"
#include <openssl/ssl.h>

namespace cd {

// Represents an object that owns in-memory-cached certificates and keys for use
// in authentication and encryption over the wire.
//
// Primarily, encapsulates an instance of SSL_CTX. In charge of system-wide TLS
// configuration. Defaults are production-ready.
class TLSContext {
public:
  // Not default constructible. Use Create.
  TLSContext() = delete;

  ~TLSContext();

  // Creates a TLSContext.
  //
  // Fails if the underlying SSL_CTX could not be created.
  static absl::StatusOr<TLSContext> Create();

  // Copy constructible and copy assignable.
  TLSContext(const TLSContext& other);
  TLSContext& operator=(const TLSContext& other);

  // Retrieves a C-style reference to the encapsulated SSL_CTX.
  const SSL_CTX* OSSLContext() const;
  SSL_CTX* MutableOSSLContext();

private:
  // Creates a context that encapsulates an SSL_CTX (ownership is transferred).
  TLSContext(SSL_CTX* ctx);

  SSL_CTX* ctx_;
};

}

#endif  // CONDUIT_TLS_TLS_H_

