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
// File: ossl_err.cc
// -----------------------------------------------------------------------------
//
// This file defines a number of helper methods for working with OpenSSL errors.
//

#include "conduit/tls/internal/ossl_err.h"

#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include <openssl/err.h>

namespace cd {

namespace tls_internal {

std::string StringOSSLError(unsigned long err, absl::string_view message) {
  char buf[1024];
  ERR_error_string_n(err, buf, sizeof(buf));
  ERR_clear_error();
  return absl::StrFormat("%s:%s", message, buf);
}

std::string StringOSSLError(absl::string_view message) {
  return StringOSSLError(ERR_get_error(), message);
}

}

}

