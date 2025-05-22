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
// File: session.cc
// -----------------------------------------------------------------------------
//
// This file defines how TLSSession works internally.
//
// TLSSession represents a generic session with management methods that pertain
// to both clients and servers.
//

#include "conduit/tls/tls.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include <openssl/ssl.h>

#include "conduit/tls/internal/ossl_err.h"

namespace cd {

TLSSession::~TLSSession() {
  if (ssl_) {
    SSL_free(ssl_);
    ssl_ = NULL;
  }
}

TLSSession::TLSSession(const TLSSession& other) : ssl_(other.ssl_) {
  if (ssl_) {
    SSL_up_ref(ssl_);
  }
}

TLSSession& TLSSession::operator=(const TLSSession& other) {
  if (ssl_) {
    if (ssl_ == other.ssl_) {
      // Self-assignment? Nope, not allowed.
      return *this;
    }

    SSL_free(ssl_);
  }

  ssl_ = other.ssl_;
  if (ssl_) {
    SSL_up_ref(ssl_);
  }

  return *this;
}

const SSL* TLSSession::OSSLSession() const {
  return ssl_;
}

SSL* TLSSession::MutableOSSLSession() {
  return ssl_;
}

TLSSession::TLSSession(SSL* ssl) : ssl_(ssl) {
  // Nothing to do.
}

}

