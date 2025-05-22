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
// File: session_server.cc
// -----------------------------------------------------------------------------
//
// This file defines how TLSServerSession works internally.
//

#include "conduit/tls/tls.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include <openssl/ssl.h>

#include "conduit/tls/internal/ossl_err.h"

namespace cd {

absl::StatusOr<TLSServerSession> TLSServerSession::Create(TLSContext* ctx) {
  SSL* ssl = SSL_new(ctx->MutableOSSLContext());

  if (!ssl) {
    return absl::InternalError(tls_internal::StringOSSLError(
      "failed to create OpenSSL session"
    ));
  }

  // We won't request client certificates by default.
  SSL_set_verify(ssl, SSL_VERIFY_NONE, NULL);

  return TLSServerSession(ssl);
}

TLSServerSession::TLSServerSession(SSL* ssl) : TLSSession(ssl) {
  // Nothing to do.
}

}

