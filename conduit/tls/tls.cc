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
// File: tls.cc
// -----------------------------------------------------------------------------
//
// This file defines how the core TLS functionality within Conduit operates.
//

#include "conduit/tls/tls.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include <openssl/ssl.h>

#include "conduit/tls/internal/ossl_err.h"

namespace cd {

namespace {

void InitializeOSSL() {
  static volatile bool initialized = false;
  if (initialized) {
    return;
  }

  initialized = true;

  SSL_library_init();
  SSL_load_error_strings();
}

}

TLSContext::~TLSContext() {
  SSL_CTX_free(ctx_);
  ctx_ = NULL;
}

absl::StatusOr<TLSContext> TLSContext::Create() {
  InitializeOSSL();
  
  SSL_CTX* ctx = SSL_CTX_new(TLS_method());

  if (!ctx) {
    return absl::InternalError(tls_internal::StringOSSLError(
      "failed to create OpenSSL context"
    ));
  }

  // Configure the CTX to be production-ready by default.

  // TLSv1.1 and below are deprecated and should not be used in production code.
  if (!SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION)) {
    SSL_CTX_free(ctx);
    return absl::InternalError(tls_internal::StringOSSLError(
      "failed to set minimum TLS protocol version"
    ));
  }

  return TLSContext(ctx);
}

TLSContext::TLSContext(const TLSContext& other) : ctx_(other.ctx_) {
  SSL_CTX_up_ref(ctx_);
}

TLSContext& TLSContext::operator=(const TLSContext& other) {
  if (ctx_) {
    if (ctx_ == other.ctx_) {
      // Self-assignment? Nope, not allowed.
      return *this;
    }

    SSL_CTX_free(ctx_);
  }

  ctx_ = other.ctx_;
  SSL_CTX_up_ref(ctx_);
  return *this;
}

const SSL_CTX* TLSContext::OSSLContext() const {
  return ctx_;
}

SSL_CTX* TLSContext::MutableOSSLContext() {
  return ctx_;
}

TLSContext::TLSContext(SSL_CTX* ctx) : ctx_(ctx) {
  // Nothing to do.
}

}

