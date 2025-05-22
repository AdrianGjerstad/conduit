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

TLSSession::~TLSSession() {
  if (ssl_) {
    SSL_free(ssl_);
    ssl_ = NULL;
  }
}

absl::StatusOr<TLSSession> TLSSession::Create(TLSContext* ctx) {
  SSL* ssl = SSL_new(ctx->MutableOSSLContext());

  if (!ssl) {
    return absl::InternalError(tls_internal::StringOSSLError(
      "failed to create OpenSSL session"
    ));
  }

  return TLSSession(ssl);
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

TLSContext::~TLSContext() {
  if (ctx_) {
    SSL_CTX_free(ctx_);
    ctx_ = NULL;
  }
}

absl::StatusOr<TLSContext> TLSContext::Create() {
  InitializeOSSL();
  
  SSL_CTX* ctx = SSL_CTX_new(TLS_method());

  if (!ctx) {
    return absl::InternalError(tls_internal::StringOSSLError(
      "failed to create OpenSSL context"
    ));
  }

  // Configure the CTX to be production-ready by default. With the goal if
  // minimizing the memory that OpenSSL uses here, SSL_CTX will contain all of
  // the configuration shared between both clients and servers, including a set
  // of trusted CAs and algorithms. Users should use
  // TLSContext::NewClientSession or TLSContext::NewServerSession to allocate
  // SSL objects with the correct configuration for that context.

  // TLSv1.1 and below are deprecated and should not be used in production code.
  if (!SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION)) {
    SSL_CTX_free(ctx);
    return absl::InternalError(tls_internal::StringOSSLError(
      "failed to set minimum TLS protocol version"
    ));
  }

  // Load trusted CA certificates from the system's default CA store.
  // Optionally, the user may eventually add their own certificates via
  // SSL_CTX_get_cert_store and something like X509_STORE_load_locations.
  if (!SSL_CTX_set_default_verify_paths(ctx)) {
    SSL_CTX_free(ctx);
    return absl::InternalError(tls_internal::StringOSSLError(
      "failed to load the system-wide root certificate store"
    ));
  }

  // Set some options for this context.
  long opts = 0;

  // Don't expect every client to send a close_notify to shutdown a connection.
  // Take care when implementing application-layer protocols not to be
  // vulnerable to truncation attacks.
  opts |= SSL_OP_IGNORE_UNEXPECTED_EOF;

  // Prevent potential CPU-exhaustion attacks where clients frequently request
  // renegotiation of the connection's primitives and key materials.
  opts |= SSL_OP_NO_RENEGOTIATION;

  // Allegedly, most TLS servers prefer their own ciphers to that of the client.
  opts |= SSL_OP_CIPHER_SERVER_PREFERENCE;

  if (!SSL_CTX_set_options(ctx, opts)) {
    SSL_CTX_free(ctx);
    return absl::InternalError(tls_internal::StringOSSLError(
      "failed to set context options for TLS"
    ));
  }

  return TLSContext(ctx);
}

TLSContext::TLSContext(const TLSContext& other) : ctx_(other.ctx_) {
  if (ctx_) {
    SSL_CTX_up_ref(ctx_);
  }
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
  if (ctx_) {
    SSL_CTX_up_ref(ctx_);
  }

  return *this;
}

const SSL_CTX* TLSContext::OSSLContext() const {
  return ctx_;
}

SSL_CTX* TLSContext::MutableOSSLContext() {
  return ctx_;
}

absl::StatusOr<TLSSession> TLSContext::NewClientSession() {
  absl::StatusOr<TLSSession> sess_s = TLSSession::Create(this);
  if (!sess_s.ok()) {
    return sess_s.status();
  }

  auto sess = sess_s.value();

  return sess;
}

absl::StatusOr<TLSSession> TLSContext::NewServerSession() {
  absl::StatusOr<TLSSession> sess_s = TLSSession::Create(this);
  if (!sess_s.ok()) {
    return sess_s.status();
  }

  auto sess = sess_s.value();

  return sess;
}

TLSContext::TLSContext(SSL_CTX* ctx) : ctx_(ctx) {
  // Nothing to do.
}

}

